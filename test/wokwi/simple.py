# umqtt/simple.py - Simple MQTT client for MicroPython
# https://github.com/micropython/micropython-lib/blob/master/micropython/umqtt.simple/umqtt/simple.py

# This file is licensed under the MIT license.
# Full license text at: https://opensource.org/licenses/MIT

import usocket as socket
import ujson as json
import ubinascii as binascii
import uhashlib as hashlib
import struct
import time as utime


class MQTTException(Exception):
    pass


class MQTTClient:
    def __init__(self, client_id, server, port=0, user=None, password=None, keepalive=0,
                 ssl=False, ssl_params={}, client_id_prefix=b""):
        if port == 0:
            port = 8883 if ssl else 1883
        self.client_id = client_id
        self.sock = None
        self.server = server
        self.port = port
        self.ssl = ssl
        self.ssl_params = ssl_params
        self._user = user
        self._pass = password
        self._keepalive = keepalive
        self._client_id_prefix = client_id_prefix
        self._next_mid = 1
        self._cmd_queue = b""
        self._out_messages = []
        self._in_messages = []
        self._cb = None
        self._last_ping = 0
        self._last_rx = 0
        self.pid_status = {}

    def _notify(self, topic, msg):
        if self._cb:
            self._cb(topic, msg)
        else:
            self._out_messages.append((topic, msg))

    def _read(self, n):
        return self.sock.recv(n)

    def _write(self, bytes_wr):
        return self.sock.send(bytes_wr)

    def set_callback(self, f):
        self._cb = f

    def set_last_will(self, topic, msg, retain=False, qos=0):
        assert 0 <= qos <= 2
        self._last_will = (topic, msg, retain, qos)

    def connect(self, clean_session=True):
        self.sock = socket.socket()
        self.sock.setblocking(False)
        if self.ssl:
            import ussl
            self.sock = ussl.wrap_socket(self.sock, **self.ssl_params)
        self.sock.connect(socket.getaddrinfo(self.server, self.port)[0][-1])
        self._send_connack(clean_session)
        if self._last_will:
            topic, msg, retain, qos = self._last_will
            self.publish(topic, msg, retain, qos)
        self._last_rx = utime.ticks_ms()

    def _send_connack(self, clean_session):
        remaining_length = 2
        cmd = 0x10 | 0x04  # CONNACK
        self._write(struct.pack("!BB", cmd, remaining_length))
        # Flags
        flags = 0x02 if clean_session else 0x00
        self._write(struct.pack("!BB", flags, 0x00))  # Return code
        self._last_rx = utime.ticks_ms()

    def _pack_msg(self, cmd, data):
        remaining_length = len(data)
        if remaining_length > 0xFFFFF:
            raise MQTTException("Message too long")
        encoded_length = bytearray()
        while remaining_length > 0:
            encoded_length.append(remaining_length % 128)
            remaining_length //= 128
            if remaining_length > 0:
                encoded_length[-1] |= 0x80
        encoded_length.reverse()
        return bytes([cmd]) + bytes(encoded_length) + data

    def publish(self, topic, msg, retain=False, qos=0):
        if qos == 1:
            self._next_mid += 1
            msg_id = self._next_mid
            self.pid_status[msg_id] = 0
        elif qos == 2:
            raise MQTTException("QoS 2 not supported")
        cmd = 0x30
        if retain:
            cmd |= 0x01
        if qos == 1:
            cmd |= 0x02
            data = struct.pack("!H", msg_id) + topic.encode("utf-8") + msg
        else:
            data = topic.encode("utf-8") + msg
        self._write(self._pack_msg(cmd, data))
        if qos == 1:
            while msg_id not in self.pid_status or self.pid_status[msg_id] == 0:
                self.wait_msg()

    def subscribe(self, topic, qos=0):
        assert 0 <= qos <= 2
        self._next_mid += 1
        msg_id = self._next_mid
        data = struct.pack("!HH", msg_id, qos) + topic.encode("utf-8")
        cmd = 0x82
        self._write(self._pack_msg(cmd, data))
        while msg_id not in self.pid_status or self.pid_status[msg_id] == 0:
            self.wait_msg()

    def unsubscribe(self, topic):
        self._next_mid += 1
        msg_id = self._next_mid
        data = struct.pack("!H", msg_id) + topic.encode("utf-8")
        cmd = 0xA2
        self._write(self._pack_msg(cmd, data))
        while msg_id not in self.pid_status or self.pid_status[msg_id] == 0:
            self.wait_msg()

    def wait_msg(self):
        try:
            while True:
                if self._cmd_queue:
                    cmd = self._cmd_queue[0]
                    if cmd == 0x40:  # PINGRESP
                        self._cmd_queue = self._cmd_queue[1:]
                        continue
                    elif cmd == 0x20:  # PUBLISH
                        sz = (len(self._cmd_queue) - 1) // 2
                        topic_len = (self._cmd_queue[1] << 8) | self._cmd_queue[2]
                        topic = self._cmd_queue[3:3 + topic_len].decode("utf-8")
                        msg = self._cmd_queue[3 + topic_len:]
                        self._cmd_queue = b""
                        self._notify(topic, msg)
                        continue
                    else:
                        sz = 0
                        while True:
                            if not self._cmd_queue:
                                break
                            o = self._cmd_queue.pop(0)
                            if o & 0x80:
                                sz = (sz << 7) | (o & 0x7F)
                            else:
                                sz = (sz << 7) | o
                                break
                        self._cmd_queue = self._cmd_queue[sz:]
                        continue
                self.sock.setblocking(True)
                try:
                    b = self._read(1)
                    self._cmd_queue += b
                    self._last_rx = utime.ticks_ms()
                finally:
                    self.sock.setblocking(False)
                if utime.ticks_diff(utime.ticks_ms(), self._last_rx) > self._keepalive * 1000 * 1.5:
                    raise MQTTException("Keepalive timeout")
        except OSError as e:
            if e.args[0] == 110:  # ETIMEDOUT
                raise MQTTException("Connection timed out")
            raise

    def check_msg(self):
        self.wait_msg()

    def disconnect(self):
        self._write(self._pack_msg(0xE0, b""))
        self.sock.close()

    def ping(self):
        self._write(self._pack_msg(0xC0, b""))
        self._last_ping = utime.ticks_ms()

# End of umqtt/simple.py
