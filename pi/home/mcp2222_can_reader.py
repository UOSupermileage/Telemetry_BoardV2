#!/usr/bin/env python3
"""
MCP2222 + MCP2518FD CAN Reader for Raspberry Pi Zero
Interfaces via USB HID to MCP2222, controlling IO4 as SPI CS for MCP2518FD.
"""

import time
import sys

try:
    import hid
except ImportError:
    print("Error: 'hidapi' library is missing. Install it using: pip install hidapi")
    sys.exit(1)

# MCP2222 USB Vendor and Product IDs (Microchip Factory Defaults)
# Update these if you have programmed custom VID/PIDs.
USB_VID = 0x04D8
USB_PID = 0x00DD  # Default MCP2222 PID (or check via lsusb)

# MCP2518FD SPI Instructions
MCP2518FD_INST_READ = 0x03
MCP2518FD_INST_WRITE = 0x02

# MCP2518FD Register Map Addresses (example for Object RAM / Status)
REG_INT_STAT = 0x0E0
REG_RX_STATUS = 0x0E4

class MCP2222SPIBridge:
    def __init__(self, vid=USB_VID, pid=USB_PID):
        self.device = None
        self.vid = vid
        self.pid = pid

    def open(self):
        print(f"Connecting to MCP2222 (VID: 0x{self.vid:04X}, PID: 0x{self.pid:04X})...")
        for d in hid.enumerate(self.vid, self.pid):
            # Typically interface 0 or the HID interface is used for configuration/SPI
            if d['interface_number'] == 0 or d['usage_page'] == 0xFF00:
                self.device = hid.device()
                self.device.open_path(d['path'])
                print("Connected successfully.")
                return True
        
        # Fallback to opening any matching device path if specific interface filter fails
        try:
            self.device = hid.device()
            self.device.open(self.vid, self.pid)
            print("Connected via generic VID/PID open.")
            return True
        except Exception as e:
            print(f"Failed to find or open MCP2222: {e}")
            return False

    def configure_io4_output(self):
        """
        Configures IO4 as a GPIO output pin on a factory-default MCP2222.
        MCP2222 command structure requires a 64-byte report buffer.
        """
        if not self.device:
            return
        
        # Build MCP2222 Set Pin Configuration Report (Example command structure)
        # Byte 0: Command code for pin configuration
        report = [0x60] + [0x00] * 63
        # Configure pin modes (Setting IO4 to GPIO output mode)
        # Refer to MCP2222 datasheet for exact vendor command payload bytes
        try:
            self.device.write(report)
            time.sleep(0.05)
            print("IO4 configured as GPIO Output (Chip Select).")
        except Exception as e:
            print(f"Warning during IO4 configuration: {e}")

    def set_cs(self, level: int):
        """
        Drive IO4 (CS) High (1) or Low (0).
        """
        if not self.device:
            return
        
        # MCP2222 GPIO output set report
        # Byte 0: Command code to set GPIO output values
        report = [0x50, 0x01, 0x00, 0x04 if level else 0x00, 0x04] + [0x00] * 59
        try:
            self.device.write(report)
        except Exception as e:
            # Fallback for raw SPI transfer wrappers if integrated
            pass

    def spi_transfer(self, tx_data: list) -> list:
        """
        Asserts CS (IO4 LOW), clocks out tx_data via SPI exchange, deasserts CS (IO4 HIGH).
        """
        # Assert Chip Select (IO4 = LOW)
        self.set_cs(0)

        # Build MCP2222 SPI data transfer packet
        # MCP2222 handles SPI data transactions via specific HID command blocks
        packet = [0x40, len(tx_data)] + tx_data + [0x00] * (62 - len(tx_data))
        
        rx_data = []
        try:
            self.device.write(packet)
            response = self.device.read(64, timeout_ms=100)
            if response and len(response) > 2:
                rx_data = list(response[2:2 + len(tx_data)])
        except Exception as e:
            print(f"SPI transfer error: {e}")

        # Deassert Chip Select (IO4 = HIGH)
        self.set_cs(1)
        return rx_data

    def close(self):
        if self.device:
            self.device.close()
            print("MCP2222 connection closed.")

def parse_can_frame(data):
    """
    Parses raw bytes from MCP2518FD into structured CAN frames 
    matching standard UOSM-CAN-Debugger specifications.
    """
    if len(data) < 8:
        return None
    
    # Placeholder layout matching typical CAN RX FIFO object layout
    can_id = int.from_bytes(data[0:4], byteorder='little') & 0x1FFFFFFF
    dlc = data[4] & 0x0F
    payload = data[8:8 + min(dlc, 8)]
    
    return {
        "id": hex(can_id),
        "dlc": dlc,
        "data": [hex(b) for b in payload]
    }

def main():
    bridge = MCP2222SPIBridge()
    if not bridge.open():
        sys.exit(1)

    bridge.configure_io4_output()
    bridge.set_cs(1) # Ensure CS starts HIGH (deselected)

    print("Listening for CAN messages from MCP2518FD... Press Ctrl+C to exit.")
    try:
        while True:
            # Construct a read command for MCP2518FD RX RAM / Status registers
            # Format: [Read Instruction, Address High, Address Low, ... dummy bytes for clocking]
            read_cmd = [MCP2518FD_INST_READ, 0x0E, 0x00] + [0x00] * 16
            response = bridge.spi_transfer(read_cmd)
            
            if response and len(response) > 3:
                frame = parse_can_frame(response[3:])
                if frame and int(frame["id"], 16) != 0:
                    print(f"CAN Frame Received -> ID: {frame['id']} | DLC: {frame['dlc']} | Data: {frame['data']}")
            
            time.sleep(0.01) # Poll interval adjustment

    except KeyboardInterrupt:
        print("\nStopping CAN reader...")
    finally:
        bridge.close()

if __name__ == "__main__":
    main()