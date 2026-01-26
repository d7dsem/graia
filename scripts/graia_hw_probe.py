#!/usr/bin/env python3
"""
graia_hw_probe - Hardware capabilities detection for GRAIA capture system
Probes PC hardware parameters: NICs, CPU topology, block devices, memory
Outputs: graia_hw_manifest_<date>.json
"""

import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional


def run_cmd(cmd: List[str]) -> Optional[str]:
    """Execute command and return output or None on failure"""
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def probe_nic(interface: str) -> Dict:
    """Probe NIC capabilities for given interface"""
    info = {"interface": interface}
    
    # Ring buffer size
    ethtool_g = run_cmd(["ethtool", "-g", interface])
    info["rx_ring_max"] = None
    info["rx_ring_current"] = None
    if ethtool_g:
        lines = ethtool_g.split('\n')
        in_pre_set = False
        in_current = False
        
        for line in lines:
            line = line.strip()
            
            if 'Pre-set maximums:' in line:
                in_pre_set = True
                in_current = False
                continue
            elif 'Current hardware settings:' in line:
                in_pre_set = False
                in_current = True
                continue
            
            if in_pre_set and line.startswith('RX:'):
                try:
                    info["rx_ring_max"] = int(line.split(':')[1].strip())
                except (ValueError, IndexError):                    
                    pass
            
            if in_current and line.startswith('RX:'):
                try:
                    info["rx_ring_current"] = int(line.split(':')[1].strip())
                except (ValueError, IndexError):
                    pass
    
    # Offload features
    info["offloads"] = None
    ethtool_k = run_cmd(["ethtool", "-k", interface])
    if ethtool_k:
        offloads = {}
        for line in ethtool_k.split('\n'):
            if ':' in line:
                feature, status = line.split(':', 1)
                offloads[feature.strip()] = status.strip()
        info["offloads"] = offloads
    
    # Link speed
    ethtool = run_cmd(["ethtool", interface])
    info["speed"] = None
    if ethtool:
        for line in ethtool.split('\n'):
            if 'Speed:' in line:
                info["speed"] = line.split(':')[1].strip()
    
    return info


def probe_kernel_limits() -> Dict:
    """Probe kernel networking and pipe limits"""
    limits = {
        "socket_rcv_buffer_max_bytes": None,
        "socket_snd_buffer_max_bytes": None,
        "pipe_max_size_bytes": None
    }

    # Socket receive buffer max
    rmem_max = Path("/proc/sys/net/core/rmem_max")
    if rmem_max.exists():
        limits["socket_rcv_buffer_max_bytes"] = int(rmem_max.read_text().strip())

    # Socket send buffer max
    wmem_max = Path("/proc/sys/net/core/wmem_max")
    if wmem_max.exists():
        limits["socket_snd_buffer_max_bytes"] = int(wmem_max.read_text().strip())
    
    # Pipe max size
    pipe_max = Path("/proc/sys/fs/pipe-max-size")
    if pipe_max.exists():
        limits["pipe_max_size_bytes"] = int(pipe_max.read_text().strip())
    
    return limits

def probe_cpu() -> Dict:
    """Probe CPU topology and capabilities"""
    info = {}
    
    # Core count and topology
    lscpu = run_cmd(["lscpu", "-p=CPU,CORE,SOCKET,NODE"])
    if lscpu:
        cores = []
        for line in lscpu.split('\n'):
            if line.startswith('#'):
                continue
            parts = line.split(',')
            if len(parts) >= 4:
                cores.append({
                    "cpu": int(parts[0]),
                    "core": int(parts[1]),
                    "socket": int(parts[2]),
                    "numa_node": int(parts[3])
                })
        info["cores"] = cores
        info["total_cores"] = len(cores)
    
    # NUMA nodes
    numactl = run_cmd(["numactl", "--hardware"])
    if numactl:
        numa_nodes = []
        for line in numactl.split('\n'):
            if line.startswith('node') and 'cpus:' in line:
                node_id = int(line.split()[1])
                cpus = [int(x) for x in line.split('cpus:')[1].split()]
                numa_nodes.append({"node": node_id, "cpus": cpus})
        info["numa_nodes"] = numa_nodes
    
    return info

def probe_block_devices() -> List[Dict]:
    """Probe all block devices in the system"""
    devices = []
    
    # Get list of block devices with size in bytes
    lsblk = run_cmd(["lsblk", "-ndo", "NAME,SIZE,TYPE", "-b"])
    if not lsblk:
        return devices
    
    for line in lsblk.split('\n'):
        parts = line.split()
        if len(parts) < 3:
            continue
        
        dev_name = parts[0]
        dev_size_bytes = int(parts[1])
        dev_type = parts[2]
        
        # Only process disks, not partitions or other types
        if dev_type != "disk":
            continue
        
        dev_info = {
            "name": dev_name,
            "size_gb": round(dev_size_bytes / (1024**3), 2),
            "device": f"/dev/{dev_name}",
            "partitions": []
        }
        
        # Get partitions for this disk
        lsblk_parts = run_cmd(["lsblk", "-nlo", "NAME,SIZE,MOUNTPOINT", "-b", f"/dev/{dev_name}"])
        if lsblk_parts:
            for part_line in lsblk_parts.split('\n')[1:]:  # Skip first line (disk itself)
                part_parts = part_line.split()
                if len(part_parts) >= 2:
                    part_name = part_parts[0]
                    part_size = int(part_parts[1])
                    mount_point = part_parts[2] if len(part_parts) >= 3 else None
                    
                    part_info = {
                        "name": part_name,
                        "size_gb": round(part_size / (1024**3), 2)
                    }
                    
                    # Get filesystem usage if mounted
                    if mount_point:
                        part_info["mount_point"] = mount_point
                        df_output = run_cmd(["df", "-B1", mount_point])
                        if df_output:
                            lines = df_output.split('\n')
                            if len(lines) > 1:
                                df_parts = lines[1].split()
                                if len(df_parts) >= 4:
                                    part_info["total_gb"] = round(int(df_parts[1]) / (1024**3), 2)
                                    part_info["used_gb"] = round(int(df_parts[2]) / (1024**3), 2)
                                    part_info["available_gb"] = round(int(df_parts[3]) / (1024**3), 2)
                    
                    dev_info["partitions"].append(part_info)
        
        # Probe device parameters from sysfs
        sys_block = Path(f"/sys/block/{dev_name}")
        if sys_block.exists():
            # Disk type (rotational)
            rotational_file = sys_block / "queue/rotational"
            if rotational_file.exists():
                is_rotational = int(rotational_file.read_text().strip())
                dev_info["type"] = "HDD" if is_rotational else "SSD/NVMe"
            
            # Block sizes
            queue = sys_block / "queue"
            hw_sector = queue / "hw_sector_size"
            phys_block = queue / "physical_block_size"
            
            if hw_sector.exists():
                dev_info["hw_sector_size"] = int(hw_sector.read_text().strip())
            if phys_block.exists():
                dev_info["physical_block_size"] = int(phys_block.read_text().strip())
            
            # Scheduler
            sched_file = queue / "scheduler"
            if sched_file.exists():
                sched = sched_file.read_text().strip()
                current = [s.strip('[]') for s in sched.split() if s.startswith('[')]
                if current:
                    dev_info["scheduler"] = current[0]
        
        devices.append(dev_info)
    
    return devices


def probe_memory() -> Dict:
    """Probe memory configuration"""
    info = {}
    
    meminfo = Path("/proc/meminfo")
    if meminfo.exists():
        for line in meminfo.read_text().split('\n'):
            if 'MemTotal:' in line:
                total_kb = int(line.split(':')[1].strip().split()[0])
                info["total_mb"] = round(total_kb / 1024, 2)
            elif 'MemAvailable:' in line:
                avail_kb = int(line.split(':')[1].strip().split()[0])
                info["available_mb"] = round(avail_kb / 1024, 2)
            elif 'HugePages_Total:' in line:
                info["hugepages_total"] = int(line.split(':')[1].strip().split()[0])
            elif 'HugePages_Free:' in line:
                info["hugepages_free"] = int(line.split(':')[1].strip().split()[0])
            elif 'Hugepagesize:' in line:
                info["hugepage_size_kb"] = int(line.split(':')[1].strip().split()[0])
    
    return info




def main():
    out_dir = Path(".")
    if len(sys.argv) < 3 or sys.argv[1] != "-o":        
        print(f"[WARN] Usage: {sys.argv[0]} -o <output-dir>. Using default: '.'", file=sys.stderr)
    else:
        out_dir = Path(sys.argv[2])
        
        if not out_dir.exists():
            print(f"ERROR: Directory does not exist: {out_dir}", file=sys.stderr)
            print(f"Create it: mkdir -p {out_dir}", file=sys.stderr)
            sys.exit(1)
        
        if not out_dir.is_dir():
            print(f"ERROR: Path is not a directory: {out_dir}", file=sys.stderr)
            sys.exit(1)
    
    ts = datetime.now()
    timestamp = ts.strftime("%Y-%m-%d")
    output_file = out_dir / f"graia_hw_manifest_{timestamp}.json"

    manifest = {
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "nics": {},
        "cpu": probe_cpu(),
        "block_devices": probe_block_devices(),
        "memory": probe_memory(),
        "limits": probe_kernel_limits()
    }
    
    # Probe NICs
    for iface in ["enp3s0fp0", "enp3s1fp1"]:
        nic_info = probe_nic(iface)
        if nic_info:
            manifest["nics"][iface] = nic_info
    
    with open(output_file, 'w') as f:
        json.dump(manifest, f, indent=2)
    
    print(f"Hardware manifest written to {output_file}")
    print(f"\nSummary:")
    print(f"  CPU: {manifest['cpu']['total_cores']} cores")
    print(f"  RAM: {manifest['memory']['total_mb']} MB (available: {manifest['memory']['available_mb']} MB)")
    print(f"  Block devices:")
    for dev in manifest['block_devices']:
        print(f"    {dev['device']}: {dev['size_gb']} GB ({dev['type']})")
        if dev['partitions']:
            for part in dev['partitions']:
                if 'mount_point' in part:
                    print(f"      {part['name']}: {part['mount_point']} - {part['available_gb']}/{part['total_gb']} GB available")
                else:
                    print(f"      {part['name']}: {part['size_gb']} GB (not mounted)")
    print(f"  NICs: {', '.join(manifest['nics'].keys())}")


if __name__ == "__main__":
    main()