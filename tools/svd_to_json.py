#!/usr/bin/env python3
"""
SVD to JSON converter for AURIX MCU register definitions.

Parses CMSIS-SVD XML files and extracts peripheral/register/field information
into a compact JSON format for runtime loading by dflash.

Usage:
    python svd_to_json.py <svd_file> <output_json> [--peripherals DMU,FLASH0,...]
    python svd_to_json.py data/SVD/tc33xpd/1.2-3/device.svd data/RegisterDefs/TC33x.json
"""

import xml.etree.ElementTree as ET
import json
import sys
import os
import argparse


def parse_int(s):
    """Parse integer from SVD hex/decimal string."""
    if s is None:
        return 0
    s = s.strip()
    if s.startswith("0x") or s.startswith("0X"):
        return int(s, 16)
    if s.startswith("#"):
        return int(s[2:], 2)
    return int(s)


def parse_enumerated_values(ev_elem):
    """Parse <enumeratedValues> element."""
    values = []
    for ev in ev_elem.findall("enumeratedValue"):
        name_elem = ev.find("name")
        desc_elem = ev.find("description")
        val_elem = ev.find("value")
        if name_elem is not None and val_elem is not None:
            values.append({
                "name": name_elem.text.strip(),
                "desc": desc_elem.text.strip() if desc_elem is not None and desc_elem.text else "",
                "value": parse_int(val_elem.text)
            })
    return values


def parse_field(field_elem):
    """Parse a <field> element."""
    name_elem = field_elem.find("name")
    desc_elem = field_elem.find("description")
    lsb_elem = field_elem.find("lsb")
    msb_elem = field_elem.find("msb")
    access_elem = field_elem.find("access")

    field = {
        "name": name_elem.text.strip() if name_elem is not None and name_elem.text else "",
        "desc": desc_elem.text.strip() if desc_elem is not None and desc_elem.text else "",
        "lsb": parse_int(lsb_elem.text if lsb_elem is not None else "0"),
        "msb": parse_int(msb_elem.text if msb_elem is not None else "0"),
    }

    if access_elem is not None and access_elem.text:
        field["access"] = access_elem.text.strip()

    # Parse enumerated values
    evs = field_elem.find("enumeratedValues")
    if evs is not None:
        values = parse_enumerated_values(evs)
        if values:
            field["values"] = values

    return field


def parse_register(reg_elem, base_addr, cluster_name=""):
    """Parse a <register> element, computing absolute address."""
    name_elem = reg_elem.find("name")
    desc_elem = reg_elem.find("description")
    offset_elem = reg_elem.find("addressOffset")
    size_elem = reg_elem.find("size")
    access_elem = reg_elem.find("access")
    reset_elem = reg_elem.find("resetValue")

    if name_elem is None or offset_elem is None:
        return None

    name = name_elem.text.strip()
    offset = parse_int(offset_elem.text)
    abs_addr = base_addr + offset

    reg = {
        "name": name,
        "offset": f"0x{offset:X}",
        "address": f"0x{abs_addr:X}",
        "size": parse_int(size_elem.text) if size_elem is not None else 32,
    }

    if desc_elem is not None and desc_elem.text:
        reg["desc"] = desc_elem.text.strip()

    if access_elem is not None and access_elem.text:
        reg["access"] = access_elem.text.strip()

    if reset_elem is not None and reset_elem.text:
        reg["resetValue"] = f"0x{parse_int(reset_elem.text):08X}"

    # Build full name: PERIPHERAL.CLUSTER.REGISTER or PERIPHERAL.REGISTER
    full_name = f"{cluster_name}.{name}" if cluster_name else name
    reg["fullName"] = full_name

    # Parse fields
    fields_elem = reg_elem.find("fields")
    if fields_elem is not None:
        fields = []
        for field_elem in fields_elem.findall("field"):
            f = parse_field(field_elem)
            if f["name"]:
                fields.append(f)
        if fields:
            reg["fields"] = fields

    return reg


def parse_cluster(cluster_elem, base_addr):
    """Parse a <cluster> element within <registers>."""
    name_elem = cluster_elem.find("name")
    offset_elem = cluster_elem.find("addressOffset")

    if name_elem is None:
        return None

    cluster_name = name_elem.text.strip()
    cluster_offset = parse_int(offset_elem.text) if offset_elem is not None else 0
    cluster_base = base_addr + cluster_offset

    cluster = {
        "name": cluster_name,
        "offset": f"0x{cluster_offset:X}",
        "registers": []
    }

    # Parse registers within cluster
    for reg_elem in cluster_elem.findall("register"):
        reg = parse_register(reg_elem, cluster_base, cluster_name)
        if reg:
            cluster["registers"].append(reg)

    return cluster


def parse_peripheral(periph_elem):
    """Parse a <peripheral> element."""
    name_elem = periph_elem.find("name")
    base_elem = periph_elem.find("baseAddress")

    if name_elem is None or base_elem is None:
        return None

    name = name_elem.text.strip()
    base_addr = parse_int(base_elem.text)

    periph = {
        "name": name,
        "baseAddress": f"0x{base_addr:X}",
    }

    # Parse addressBlock
    addr_block = periph_elem.find("addressBlock")
    if addr_block is not None:
        size_elem = addr_block.find("size")
        if size_elem is not None:
            periph["size"] = f"0x{parse_int(size_elem.text):X}"

    regs_elem = periph_elem.find("registers")
    if regs_elem is None:
        return periph

    clusters = []
    registers = []

    for child in regs_elem:
        if child.tag == "cluster":
            cluster = parse_cluster(child, base_addr)
            if cluster and cluster["registers"]:
                clusters.append(cluster)
        elif child.tag == "register":
            reg = parse_register(child, base_addr)
            if reg:
                registers.append(reg)

    if clusters:
        periph["clusters"] = clusters
    if registers:
        periph["registers"] = registers

    return periph


def svd_to_json(svd_path, peripheral_filter=None):
    """Parse SVD file and return structured dict."""
    tree = ET.parse(svd_path)
    root = tree.getroot()

    device_name_elem = root.find("name")
    device_name = device_name_elem.text.strip() if device_name_elem is not None and device_name_elem.text else "Unknown"

    result = {
        "device": device_name,
        "peripherals": []
    }

    peripherals_elem = root.find(".//peripherals")
    if peripherals_elem is None:
        print(f"WARNING: No <peripherals> found in {svd_path}", file=sys.stderr)
        return result

    for periph_elem in peripherals_elem.findall("peripheral"):
        periph = parse_peripheral(periph_elem)
        if periph is None:
            continue

        # Apply filter if specified
        if peripheral_filter:
            if periph["name"] not in peripheral_filter:
                continue

        # Only include peripherals that have registers
        if "clusters" in periph or "registers" in periph:
            result["peripherals"].append(periph)

    return result


def main():
    parser = argparse.ArgumentParser(description="Convert CMSIS-SVD to compact JSON")
    parser.add_argument("svd_file", help="Input SVD XML file path")
    parser.add_argument("output_json", help="Output JSON file path")
    parser.add_argument("--peripherals", "-p",
                       help="Comma-separated list of peripheral names to include (default: Flash-related subset)",
                       default=None)
    parser.add_argument("--all", "-a", action="store_true",
                       help="Include all peripherals (no filter)")
    args = parser.parse_args()

    if not os.path.isfile(args.svd_file):
        print(f"ERROR: SVD file not found: {args.svd_file}", file=sys.stderr)
        sys.exit(1)

    # Default whitelist: Flash/debug related peripherals
    DEFAULT_PERIPHERALS = [
        "DMU",          # Direct Memory Unit (Flash control)
        "FLASH0",       # Flash Controller (TC2x)
        "PMU",          # Program Memory Unit
        "FSI",          # Flash Storage Interface
        "PFI0", "PFI1", # Program Flash Interface
        "CBS",          # Cerberus (debug interface)
        "SMU",          # Safety Monitoring Unit
        "SCU",          # System Control Unit
        "MTU",          # Memory Test Unit
    ]

    peripheral_filter = None
    if args.all:
        peripheral_filter = None
    elif args.peripherals:
        peripheral_filter = [p.strip() for p in args.peripherals.split(",")]
    else:
        peripheral_filter = DEFAULT_PERIPHERALS

    print(f"Parsing SVD: {args.svd_file}")
    result = svd_to_json(args.svd_file, peripheral_filter)

    # Create output directory if needed
    out_dir = os.path.dirname(args.output_json)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    # Write JSON
    with open(args.output_json, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    # Print summary
    num_periphs = len(result["peripherals"])
    num_regs = 0
    for p in result["peripherals"]:
        num_regs += len(p.get("registers", []))
        for c in p.get("clusters", []):
            num_regs += len(c.get("registers", []))

    print(f"Device: {result['device']}")
    print(f"Peripherals: {num_periphs}")
    print(f"Registers: {num_regs}")
    print(f"Output: {args.output_json}")


if __name__ == "__main__":
    main()
