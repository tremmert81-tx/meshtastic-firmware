#!/usr/bin/env python3
"""
Cattle Presence Gateway for Meshtastic
Receives cattle tag presence data from Meshtastic mesh network and displays it.
Future: Will send to MQTT/Supabase for cloud storage.

Usage:
    python3 cattle_gateway.py [--port SERIAL_PORT] [--host HOST] [--baud BAUDRATE]
    
Example:
    python3 cattle_gateway.py --port /dev/ttyUSB0
    python3 cattle_gateway.py --host meshtastic.local
"""

import argparse
import struct
import sys
import os
import time
from datetime import datetime
from typing import Dict, List, Optional, Tuple

try:
    import meshtastic
    import meshtastic.serial_interface
    import meshtastic.tcp_interface
    from pubsub import pub
except ImportError:
    print("ERROR: meshtastic library not installed.")
    print("Install it with: pip3 install meshtastic")
    sys.exit(1)

try:
    from supabase import create_client, Client
    from dotenv import load_dotenv
    SUPABASE_AVAILABLE = True
except ImportError:
    SUPABASE_AVAILABLE = False
    print("WARNING: supabase or python-dotenv not installed.")
    print("Supabase integration will be disabled.")
    print("Install with: pip3 install supabase python-dotenv")


# Constants matching the firmware
CATTLE_MESH_PORTNUM = 80
CATTLE_PROTOCOL_VERSION = 0x01
CATTLE_MSG_TYPE_PRESENCE = 0x42


class CattlePresenceHeader:
    """Cattle presence packet header structure"""
    STRUCT_FORMAT = "<BBHH"  # version, msgType, nodeId, tagCount (little-endian)
    SIZE = 6  # 1 + 1 + 2 + 2 bytes
    
    def __init__(self, version: int, msg_type: int, node_id: int, tag_count: int):
        self.version = version
        self.msg_type = msg_type
        self.node_id = node_id
        self.tag_count = tag_count
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'CattlePresenceHeader':
        """Parse header from bytes"""
        if len(data) < cls.SIZE:
            raise ValueError(f"Header too short: {len(data)} < {cls.SIZE}")
        version, msg_type, node_id, tag_count = struct.unpack(cls.STRUCT_FORMAT, data[:cls.SIZE])
        return cls(version, msg_type, node_id, tag_count)


class CattleTagEntry:
    """Cattle tag entry structure"""
    STRUCT_FORMAT = "<HbB"  # tagId (uint16), rssi (int8), status (uint8)
    SIZE = 4  # 2 + 1 + 1 bytes
    
    def __init__(self, tag_id: int, rssi: int, status: int):
        self.tag_id = tag_id
        self.rssi = rssi
        self.status = status
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'CattleTagEntry':
        """Parse tag entry from bytes"""
        if len(data) < cls.SIZE:
            raise ValueError(f"Tag entry too short: {len(data)} < {cls.SIZE}")
        tag_id, rssi, status = struct.unpack(cls.STRUCT_FORMAT, data[:cls.SIZE])
        return cls(tag_id, rssi, status)


class CattlePresenceDecoder:
    """Decodes cattle presence packets from Meshtastic mesh"""
    
    def __init__(self):
        self.stats = {
            'packets_received': 0,
            'packets_decoded': 0,
            'packets_errors': 0,
            'tags_seen': 0,
        }
    
    def decode_packet(self, payload: bytes) -> Optional[Tuple[CattlePresenceHeader, List[CattleTagEntry]]]:
        """
        Decode a cattle presence packet
        
        Returns:
            Tuple of (header, tags) or None if decode fails
        """
        try:
            # Parse header
            if len(payload) < CattlePresenceHeader.SIZE:
                return None
            
            header = CattlePresenceHeader.from_bytes(payload)
            
            # Validate header
            if header.version != CATTLE_PROTOCOL_VERSION:
                print(f"WARNING: Unexpected protocol version: {header.version}")
                return None
            
            if header.msg_type != CATTLE_MSG_TYPE_PRESENCE:
                print(f"WARNING: Unexpected message type: {header.msg_type}")
                return None
            
            # Parse tag entries
            offset = CattlePresenceHeader.SIZE
            tags = []
            
            for i in range(header.tag_count):
                if offset + CattleTagEntry.SIZE > len(payload):
                    print(f"WARNING: Packet truncated, expected {header.tag_count} tags but only {len(tags)} fit")
                    break
                
                tag = CattleTagEntry.from_bytes(payload[offset:offset + CattleTagEntry.SIZE])
                tags.append(tag)
                offset += CattleTagEntry.SIZE
            
            self.stats['packets_decoded'] += 1
            self.stats['tags_seen'] += len(tags)
            
            return (header, tags)
        
        except Exception as e:
            self.stats['packets_errors'] += 1
            print(f"ERROR decoding packet: {e}")
            return None


class CattleGateway:
    """Main gateway class for receiving and processing cattle presence data"""
    
    def __init__(self, interface):
        self.interface = interface
        self.decoder = CattlePresenceDecoder()
        self.last_seen_tags: Dict[int, Dict] = {}  # node_id -> {tag_id: last_seen_info}
        # Store instance for static callback
        CattleGateway._instance = self
        
        # Initialize Supabase client
        self.supabase: Optional[Client] = None
        self.supabase_enabled = False
        
        if SUPABASE_AVAILABLE:
            try:
                # Load environment variables from .env file
                # Try to load from scripts directory first, then current directory
                env_path = os.path.join(os.path.dirname(__file__), '.env')
                if not os.path.exists(env_path):
                    env_path = '.env'  # Fall back to current directory
                
                load_dotenv(env_path)
                
                supabase_url = os.getenv('SUPABASE_URL')
                supabase_key = os.getenv('SUPABASE_KEY')
                supabase_enabled = os.getenv('SUPABASE_ENABLED', 'true').lower() == 'true'
                
                print(f"DEBUG: Supabase config - URL: {'SET' if supabase_url else 'NOT SET'}, KEY: {'SET' if supabase_key else 'NOT SET'}, Enabled: {supabase_enabled}")
                
                if supabase_enabled and supabase_url and supabase_key:
                    self.supabase = create_client(supabase_url, supabase_key)
                    self.supabase_enabled = True
                    print("✓ Supabase client initialized successfully")
                else:
                    if not supabase_enabled:
                        print("⚠ Supabase integration disabled in .env file")
                    else:
                        print("⚠ WARNING: Supabase credentials not found in .env file")
                        print("  Looking for .env file at:", env_path)
                        print("  SUPABASE_URL:", 'SET' if supabase_url else 'NOT SET')
                        print("  SUPABASE_KEY:", 'SET' if supabase_key else 'NOT SET')
                        print("  Supabase integration will be disabled")
            except Exception as e:
                print(f"⚠ WARNING: Failed to initialize Supabase client: {e}")
                import traceback
                traceback.print_exc()
                print("Gateway will continue without database storage")
        else:
            print("⚠ Supabase libraries not available - database storage disabled")
            print("  Install with: pip3 install supabase python-dotenv")
    
    def on_receive(self, packet, interface):
        """Callback for received Meshtastic packets"""
        try:
            # Debug: inspect packet structure (only first few times to avoid spam)
            if not hasattr(self, '_debug_count'):
                self._debug_count = 0
            if self._debug_count < 3:
                print(f"DEBUG: Packet type: {type(packet)}")
                if hasattr(packet, '__dict__'):
                    print(f"DEBUG: Packet attributes: {[k for k in dir(packet) if not k.startswith('_')]}")
                elif isinstance(packet, dict):
                    print(f"DEBUG: Packet keys: {list(packet.keys())}")
                self._debug_count += 1
            
            # Try different ways to access decoded data
            decoded = None
            if hasattr(packet, 'decoded'):
                decoded = packet.decoded
            elif hasattr(packet, 'get'):
                # Might be a dict-like object
                decoded = packet.get('decoded')
            elif isinstance(packet, dict):
                decoded = packet.get('decoded')
            
            if not decoded:
                # Packets without decoded data might be control/ack packets
                # Try to get portnum directly from packet as fallback
                portnum = None
                if hasattr(packet, 'portnum'):
                    portnum = packet.portnum
                elif isinstance(packet, dict):
                    portnum = packet.get('portnum')
                    if not portnum and 'decoded' in packet:
                        decoded_obj = packet['decoded']
                        if isinstance(decoded_obj, dict):
                            portnum = decoded_obj.get('portnum')
                        elif hasattr(decoded_obj, 'portnum'):
                            portnum = decoded_obj.portnum
                
                # If we found a portnum, log it for debugging
                if portnum:
                    from_node = f"0x{packet.get('from', 'unknown'):X}" if isinstance(packet, dict) else "unknown"
                    print(f"DEBUG: Received packet on port {portnum} from {from_node} (no decoded payload)")
                
                # Skip packets without decoded data (they're likely acks or control packets)
                return
            
            # Now we have decoded data - try to get portnum
            portnum = None
            if hasattr(decoded, 'portnum'):
                portnum = decoded.portnum
            elif isinstance(decoded, dict):
                portnum = decoded.get('portnum')
            
            if portnum is None:
                return
            
            # Handle portnum as string, int, or enum
            portnum_int = None
            if isinstance(portnum, int):
                portnum_int = portnum
            elif isinstance(portnum, str):
                # Try to convert string to int
                try:
                    portnum_int = int(portnum)
                except:
                    # Might be an enum name like "POSITION_APP", check if it's 80
                    if '80' in str(portnum) or portnum == 'CATTLE_MESH_PORTNUM':
                        portnum_int = 80
            else:
                # Might be an enum, try to get its value
                try:
                    portnum_int = int(portnum)
                except:
                    pass
            
            from_node = f"0x{packet.from_:X}" if hasattr(packet, 'from_') else "unknown"
            print(f"DEBUG: Received packet on port {portnum} (int: {portnum_int}) from {from_node}")
            
            # Check if this is a cattle presence packet (port 80)
            if portnum_int != CATTLE_MESH_PORTNUM:
                return  # Not a cattle packet
            
            self.decoder.stats['packets_received'] += 1
            print(f"DEBUG: Cattle packet received!")
            
            # Get payload - try different access methods
            payload = None
            if hasattr(decoded, 'payload'):
                payload = decoded.payload
            elif isinstance(decoded, dict):
                payload = decoded.get('payload')
            
            if not payload:
                print("DEBUG: No payload in packet")
                return
            
            # Handle different payload formats
            if isinstance(payload, bytes):
                payload_bytes = payload
            elif isinstance(payload, str):
                payload_bytes = payload.encode('latin1')
            elif hasattr(payload, 'bytes'):
                payload_bytes = payload.bytes
            else:
                print(f"DEBUG: Unknown payload type: {type(payload)}")
                return
            
            print(f"DEBUG: Payload size: {len(payload_bytes)} bytes")
            
            # Decode the packet
            result = self.decoder.decode_packet(payload_bytes)
            if not result:
                print("DEBUG: Failed to decode packet")
                return
            
            header, tags = result
            
            # Display the data
            self.display_cattle_presence(packet, header, tags)
            
            # Store for future MQTT/Supabase integration
            self.store_presence_data(packet, header, tags)
        
        except Exception as e:
            print(f"ERROR in on_receive: {e}")
            import traceback
            traceback.print_exc()
    
    @staticmethod
    def on_receive_static(packet, interface=None):
        """Static wrapper for pubsub callback"""
        # The pubsub callback might pass different arguments
        # Try to get the gateway instance from a global or the interface
        if interface and hasattr(interface, '_cattle_gateway'):
            interface._cattle_gateway.on_receive(packet, interface)
        elif hasattr(CattleGateway, '_instance'):
            # Fallback: use a class-level instance
            CattleGateway._instance.on_receive(packet, interface)
    
    def display_cattle_presence(self, packet, header: CattlePresenceHeader, tags: List[CattleTagEntry]):
        """Display cattle presence data to terminal"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        # Try different ways to get from node
        from_node = "unknown"
        if hasattr(packet, 'from_'):
            from_node = f"0x{packet.from_:X}"
        elif hasattr(packet, 'from'):
            # 'from' is a Python keyword, so use getattr
            from_val = getattr(packet, 'from', None)
            if from_val is not None:
                from_node = f"0x{from_val:X}"
        elif isinstance(packet, dict):
            from_val = packet.get('from')
            if from_val is not None:
                from_node = f"0x{from_val:X}"
        
        print(f"\n{'='*60}")
        print(f"[{timestamp}] Cattle Presence Report")
        print(f"{'='*60}")
        print(f"From Node: {from_node} (ID: {header.node_id})")
        print(f"Tags Detected: {len(tags)}")
        print(f"{'-'*60}")
        
        if tags:
            print(f"{'Tag ID':<10} {'RSSI (dBm)':<12} {'Status':<10} {'Distance Est':<15}")
            print(f"{'-'*60}")
            
            for tag in tags:
                # Estimate distance from RSSI (rough approximation)
                # RSSI of -30 to -50: very close (< 1m)
                # RSSI of -50 to -70: close (1-5m)
                # RSSI of -70 to -90: medium (5-20m)
                # RSSI of -90 to -110: far (> 20m)
                if tag.rssi >= -50:
                    dist_est = "< 1m"
                elif tag.rssi >= -70:
                    dist_est = "1-5m"
                elif tag.rssi >= -90:
                    dist_est = "5-20m"
                else:
                    dist_est = "> 20m"
                
                status_str = f"0x{tag.status:02X}"
                print(f"{tag.tag_id:<10} {tag.rssi:<12} {status_str:<10} {dist_est:<15}")
        else:
            print("No tags detected")
        
        print(f"{'='*60}\n")
    
    def store_presence_data(self, packet, header: CattlePresenceHeader, tags: List[CattleTagEntry]):
        """
        Store presence data to Supabase database
        """
        # Store in memory for now (for backward compatibility)
        node_id = header.node_id
        if node_id not in self.last_seen_tags:
            self.last_seen_tags[node_id] = {}
        
        for tag in tags:
            self.last_seen_tags[node_id][tag.tag_id] = {
                'rssi': tag.rssi,
                'status': tag.status,
                'timestamp': datetime.now().isoformat(),
            }
        
        # Store to Supabase if enabled
        if not self.supabase_enabled or not self.supabase:
            if not hasattr(self, '_supabase_warning_shown'):
                print("⚠ Supabase not enabled - data will not be stored to database")
                self._supabase_warning_shown = True
            return
        
        try:
            # Get from_node_hex from packet
            from_node_hex = "unknown"
            if hasattr(packet, 'from_'):
                from_node_hex = f"0x{packet.from_:X}"
            elif isinstance(packet, dict):
                from_val = packet.get('from')
                if from_val is not None:
                    from_node_hex = f"0x{from_val:X}"
            
            # Calculate distance estimates for tags
            def get_distance_estimate(rssi: int) -> str:
                if rssi >= -50:
                    return "< 1m"
                elif rssi >= -70:
                    return "1-5m"
                elif rssi >= -90:
                    return "5-20m"
                else:
                    return "> 20m"
            
            # Insert report record
            max_retries = int(os.getenv('SUPABASE_MAX_RETRIES', '3'))
            retry_delay = float(os.getenv('SUPABASE_RETRY_DELAY', '2'))
            
            report_data = {
                'node_id': node_id,
                'from_node_hex': from_node_hex,
                'tag_count': len(tags),
                'received_at': datetime.now().isoformat()
            }
            
            report_id = None
            for attempt in range(max_retries):
                try:
                    response = self.supabase.table('cattle_presence_reports').insert(report_data).execute()
                    if response.data and len(response.data) > 0:
                        report_id = response.data[0]['id']
                        break
                except Exception as e:
                    if attempt < max_retries - 1:
                        print(f"WARNING: Supabase insert failed (attempt {attempt + 1}/{max_retries}): {e}")
                        time.sleep(retry_delay * (attempt + 1))  # Exponential backoff
                    else:
                        print(f"ERROR: Failed to insert report to Supabase after {max_retries} attempts: {e}")
                        return
            
            if not report_id:
                print("ERROR: Failed to get report ID from Supabase")
                return
            
            # Batch insert tag readings
            readings_data = []
            for tag in tags:
                readings_data.append({
                    'report_id': report_id,
                    'tag_id': tag.tag_id,
                    'rssi': tag.rssi,
                    'status': tag.status,
                    'distance_estimate': get_distance_estimate(tag.rssi)
                })
            
            # Insert all readings in one batch
            for attempt in range(max_retries):
                try:
                    self.supabase.table('cattle_tag_readings').insert(readings_data).execute()
                    print(f"✓ Stored {len(tags)} tag readings to Supabase (report_id: {report_id[:8]}...)")
                    break
                except Exception as e:
                    if attempt < max_retries - 1:
                        print(f"WARNING: Supabase readings insert failed (attempt {attempt + 1}/{max_retries}): {e}")
                        time.sleep(retry_delay * (attempt + 1))
                    else:
                        print(f"ERROR: Failed to insert tag readings to Supabase after {max_retries} attempts: {e}")
        
        except Exception as e:
            print(f"ERROR: Unexpected error storing to Supabase: {e}")
            import traceback
            traceback.print_exc()
    
    def print_stats(self):
        """Print statistics about received packets"""
        stats = self.decoder.stats
        print(f"\n{'='*60}")
        print("Statistics:")
        print(f"  Packets Received: {stats['packets_received']}")
        print(f"  Packets Decoded: {stats['packets_decoded']}")
        print(f"  Decode Errors: {stats['packets_errors']}")
        print(f"  Total Tags Seen: {stats['tags_seen']}")
        print(f"{'='*60}\n")
    
    def run(self):
        """Start the gateway and listen for packets"""
        print("Cattle Presence Gateway Starting...")
        print(f"Listening for cattle packets on port {CATTLE_MESH_PORTNUM}")
        print("Press Ctrl+C to stop\n")
        
        # Store gateway instance in interface for static callback
        self.interface._cattle_gateway = self
        
        # Subscribe to receive events using pubsub
        # Try multiple topic names in case the API changed
        try:
            pub.subscribe(self.on_receive_static, "meshtastic.receive")
            print("Subscribed to 'meshtastic.receive' events")
        except Exception as e:
            print(f"Warning: Could not subscribe to 'meshtastic.receive': {e}")
            print("Trying alternative subscription methods...")
        
        # Also try subscribing to the interface's callback if it has one
        if hasattr(self.interface, 'setConnected'):
            print("Interface supports setConnected")
        
        print("Waiting for packets...")
        print("(If you don't see 'DEBUG: Received packet' messages, the callback may not be working)\n")
        
        try:
            # Keep running - pubsub will call our callback
            while True:
                import time
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n\nShutting down...")
            # Unsubscribe
            try:
                pub.unsubscribe(self.on_receive_static, "meshtastic.receive")
            except:
                pass
            self.print_stats()
            if hasattr(self.interface, 'close'):
                self.interface.close()
            sys.exit(0)


def main():
    parser = argparse.ArgumentParser(
        description='Cattle Presence Gateway for Meshtastic',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Connect via serial port
  python3 cattle_gateway.py --port /dev/ttyUSB0
  
  # Connect via TCP (if Meshtastic node is on network)
  python3 cattle_gateway.py --host meshtastic.local
  
  # Use specific baudrate
  python3 cattle_gateway.py --port /dev/ttyUSB0 --baud 115200
        """
    )
    
    parser.add_argument(
        '--port',
        type=str,
        help='Serial port (e.g., /dev/ttyUSB0 or COM3)'
    )
    
    parser.add_argument(
        '--host',
        type=str,
        help='TCP hostname or IP address (e.g., meshtastic.local or 192.168.1.100)'
    )
    
    parser.add_argument(
        '--baud',
        type=int,
        default=115200,
        help='Serial baudrate (default: 115200)'
    )
    
    args = parser.parse_args()
    
    # Create interface
    interface = None
    
    if args.host:
        print(f"Connecting to Meshtastic node at {args.host}...")
        try:
            interface = meshtastic.tcp_interface.TCPInterface(hostname=args.host)
        except Exception as e:
            print(f"ERROR: Failed to connect to {args.host}: {e}")
            sys.exit(1)
    
    elif args.port:
        print(f"Connecting to Meshtastic node on {args.port} at {args.baud} baud...")
        try:
            interface = meshtastic.serial_interface.SerialInterface(devPath=args.port, baud=args.baud)
        except Exception as e:
            print(f"ERROR: Failed to connect to {args.port}: {e}")
            print("Make sure the port exists and you have permission to access it.")
            sys.exit(1)
    
    else:
        # Try to auto-detect
        print("No port or host specified, attempting auto-detect...")
        try:
            interface = meshtastic.serial_interface.SerialInterface()
        except Exception as e:
            print(f"ERROR: Auto-detect failed: {e}")
            print("Please specify --port or --host")
            parser.print_help()
            sys.exit(1)
    
    # Create and run gateway
    gateway = CattleGateway(interface)
    gateway.run()


if __name__ == '__main__':
    main()

