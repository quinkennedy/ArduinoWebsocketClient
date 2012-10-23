/*
 WebsocketClient, a websocket client for Arduino
 Copyright 2011 Kevin Rohling
 Copyright 2012 Ian Moore
 http://kevinrohling.com
 http://www.incamoon.com
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <WebSocketClient.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdlib.h>

prog_char clientHandshakeLine1a[] PROGMEM = "GET ";
prog_char clientHandshakeLine1b[] PROGMEM = " HTTP/1.1";
prog_char clientHandshakeLine2[] PROGMEM = "Upgrade: WebSocket";
prog_char clientHandshakeLine3[] PROGMEM = "Connection: Upgrade";
prog_char clientHandshakeLine4[] PROGMEM = "Host: ";
prog_char clientHandshakeLine5[] PROGMEM = "Sec-WebSocket-Origin: ArduinoWebSocketClient";
prog_char clientHandshakeLine6[] PROGMEM = "Sec-WebSocket-Version: 13";
prog_char clientHandshakeLine7[] PROGMEM = "Sec-WebSocket-Key: ";
prog_char clientHandshakeLine8[] PROGMEM = "Sec-WebSocket-Protocol: ";
prog_char serverHandshake[] PROGMEM = "HTTP/1.1 101";

PROGMEM const char *WebSocketClientStringTable[] =
{
  clientHandshakeLine1a,
  clientHandshakeLine1b,
  clientHandshakeLine2,
  clientHandshakeLine3,
  clientHandshakeLine4,
  clientHandshakeLine5,
  clientHandshakeLine6,
  clientHandshakeLine7,
  clientHandshakeLine8,
  serverHandshake
};

void WebSocketClient::getStringTableItem(char* buffer, int index) {
  strcpy_P(buffer, (char*)pgm_read_word(&(WebSocketClientStringTable[index])));
}

void WebSocketClient::connect(char hostname[], int port, char protocol[], char path[]) {
  _hostname = hostname;
  _port = port;
  _protocol = protocol;
  _path = path;
  _retryTimeout = millis();
  _canConnect = true;
}

void WebSocketClient::reconnect() {
  bool result = false;
  if (_client.connect(_hostname, _port)) {
    sendHandshake(_hostname, _path, _protocol);
    result = readHandshake();
  }
  if(!result) {
    
#ifdef DEBUG
    debug(F("Connection Failed!"));
#endif
    if(_onError != NULL) {
      _onError(*this, "Connection Failed!");
    }
    _client.stop();
  } else {
      if(_onOpen != NULL) {
          _onOpen(*this);
      }
  }
}

bool WebSocketClient::connected() {
  return _client.connected();
}

void WebSocketClient::disconnect() {
  _client.stop();
}

byte WebSocketClient::nextByte() {
  while(_client.available() == 0);
  byte b = _client.read();
  
#ifdef DEBUG
  if(b < 0) {
    debug(F("Internal Error in Ethernet Client Library (-1 returned where >= 0 expected)"));
  }
#endif
  
  return b;
}

void WebSocketClient::monitor () {
  
  if(!_canConnect) {
    return;
  }
  
  if(_reconnecting) {
    return;
  }
  
  if(!connected() && millis() > _retryTimeout) {
    _retryTimeout = millis() + RETRY_TIMEOUT;
    _reconnecting = true;
    reconnect();
    _reconnecting = false;
    return;
  }
  
	if (_client.available() > 2) {
    byte hdr = nextByte();
    bool fin = hdr & 0x80;
    
#ifdef TRACE
    debug(F("fin = %x"), fin);
#endif
    
    int opCode = hdr & 0x0F;
    
#ifdef TRACE
    debug(F("op = %x"), opCode);
#endif
    
    hdr = nextByte();
    bool mask = hdr & 0x80;
    int len = hdr & 0x7F;
    if(len == 126) {
      len = nextByte();
      len <<= 8;
      len += nextByte();
    } else if (len == 127) {
      len = nextByte();
      for(int i = 0; i < 7; i++) { // NOTE: This may not be correct.  RFC 6455 defines network byte order(??). (section 5.2)
        len <<= 8;
        len += nextByte();
      }
    }
    
#ifdef TRACE
    debug(F("len = %d"), len);
#endif
    
    if(mask) { // skipping 4 bytes for now.
      for(int i = 0; i < 4; i++) {
        nextByte();
      }
    }
    
    if(mask) {
      
#ifdef DEBUG
      debug(F("Masking not yet supported (RFC 6455 section 5.3)"));
#endif
      
      if(_onError != NULL) {
        _onError(*this, "Masking not supported");
      }
      free(_packet);
      return;
    }
    
    if(!fin) {
      if(_packet == NULL) {
        _packet = (char*) malloc(len);
        for(int i = 0; i < len; i++) {
          _packet[i] = nextByte();
        }
        _packetLength = len;
        _opCode = opCode;
      } else {
        int copyLen = _packetLength;
        _packetLength += len;
        char *temp = _packet;
        _packet = (char*)malloc(_packetLength);
        for(int i = 0; i < _packetLength; i++) {
          if(i < copyLen) {
            _packet[i] = temp[i];
          } else {
            _packet[i] = nextByte();
          }
        }
        free(temp);
      }
      return;
    }
    
    if(_packet == NULL) {
      _packet = (char*) malloc(len + 1);
      for(int i = 0; i < len; i++) {
        _packet[i] = nextByte();
      }
      _packet[len] = 0x0;
    } else {
      int copyLen = _packetLength;
      _packetLength += len;
      char *temp = _packet;
      _packet = (char*) malloc(_packetLength + 1);
      for(int i = 0; i < _packetLength; i++) {
        if(i < copyLen) {
          _packet[i] = temp[i];
        } else {
          _packet[i] = nextByte();
        }
      }
      _packet[_packetLength] = 0x0;
      free(temp);
    }
    
    if(opCode == 0 && _opCode > 0) {
      opCode = _opCode;
      _opCode = 0;
    }
    
    switch(opCode) {
      case 0x00:
        
#ifdef DEBUG
        debug(F("Unexpected Continuation OpCode"));
#endif
        
        break;
        
      case 0x01:
        
#ifdef DEBUG
        debug(F("onMessage: data = %s"), _packet);
#endif
        
        if (_onMessage != NULL) {
          _onMessage(*this, _packet);
        }
        break;
        
      case 0x02:
        
#ifdef DEBUG
        debug(F("Binary messages not yet supported (RFC 6455 section 5.6)"));
#endif
        
        if(_onError != NULL) {
          _onError(*this, "Binary Messages not supported");
        }
        break;
        
      case 0x09:
        
#ifdef DEBUG
        debug(F("onPing"));
#endif
        
        _client.write(0x8A);
        _client.write(byte(0x00));
        break;
        
      case 0x0A:
        
#ifdef DEBUG
        debug(F("onPong"));
#endif
        
        break;
        
      case 0x08:
        
        unsigned int code = ((byte)_packet[0] << 8) + (byte)_packet[1];
        
#ifdef DEBUG
        debug(F("onClose: code = %d; message = %s"), code, (_packet + 2));
#endif
        
        if(_onClose != NULL) {
          _onClose(*this, code, (_packet + 2));
        }
        _client.stop();
        break;
    }
    
    free(_packet);
    _packet = NULL;
  }
}

void WebSocketClient::onMessage(OnMessage fn) {
  _onMessage = fn;
}

void WebSocketClient::onOpen(OnOpen fn) {
  _onOpen = fn;
}

void WebSocketClient::onClose(OnClose fn) {
  _onClose = fn;
}

void WebSocketClient::onError(OnError fn) {
  _onError = fn;
}


void WebSocketClient::sendHandshake(char* hostname, char* path, char* protocol) {
  
  char buffer[45];
  
  getStringTableItem(buffer, 0);
  _client.print(buffer);
  _client.print(path);
  
  getStringTableItem(buffer, 1);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 2);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 3);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 4);
  _client.print(buffer);
  _client.println(hostname);
    
#ifdef HANDSHAKE
    _debug->print(buffer);
    _debug->println(hostname);
#endif
    
  getStringTableItem(buffer, 5);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 6);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 7);
  _client.print(buffer);
    
#ifdef HANDSHAKE
    _debug->print(buffer);
#endif
    
  generateHash(buffer);
  _client.println(buffer);
    
#ifdef HANDSHAKE
    _debug->println(buffer);
#endif
    
  getStringTableItem(buffer, 8);
  _client.print(buffer);
  _client.println(protocol);
    
#ifdef HANDSHAKE
    _debug->print(buffer);
    _debug->println(protocol);
#endif
    
    _client.println(); 
    
#ifdef HANDSHAKE
    _debug->println();
#endif
    
}

bool WebSocketClient::readHandshake() {
  bool result = false;
  char line[128];
  int maxAttempts = 300, attempts = 0;
  char response[12];
  getStringTableItem(response, 9);
  
  while(_client.available() == 0 && attempts < maxAttempts)
  {
    delay(100);
    attempts++;
  }
  
  while(true) {
      readLine(line); 
#ifdef HANDSHAKE
      _debug->println(line);
#endif
      
    if(strcmp(line, "") == 0) {
      break;
    }
    if(strncmp(line, response, 12) == 0) {
      result = true;
    }
  }
  
  if(!result) {
#ifdef DEBUG
    debug(F("Handshake Failed! Terminating"));
#endif
    _client.stop();
  }
  
  return result;
}

void WebSocketClient::readLine(char* buffer) {
  char character;
  
  int i = 0;
  while(_client.available() > 0 && (character = _client.read()) != '\n') {
    if (character != '\r' && character != -1) {
      buffer[i++] = character;
    }
  }
  buffer[i] = 0x0;
}

bool WebSocketClient::send (char* message) {
  if(!_canConnect || _reconnecting) {
    return false;
  }
  int len = strlen(message);
  _client.write(0x81);
  if(len > 125) {
    _client.write(0xFE);
    _client.write(byte(len >> 8));
    _client.write(byte(len & 0xFF));
  } else {
    _client.write(0x80 | byte(len));
  }
  for(int i = 0; i < 4; i++) {
    _client.write((byte)0x00); // use 0x00 for mask bytes which is effectively a NOOP
  }
  _client.print(message);
  return true;
}

void WebSocketClient::generateHash(char buffer[], size_t bufferlen) {
  byte bytes[16];
  for(int i = 0; i < 16; i++) {
    bytes[i] = random(255);
  }
  base64Encode(bytes, 16, buffer, bufferlen);
}

size_t WebSocketClient::base64Encode(byte* src, size_t srclength, char* target, size_t targetsize) {
  
  size_t datalength = 0;
	char input[3];
	char output[4];
	size_t i;
  
	while (2 < srclength) {
		input[0] = *src++;
		input[1] = *src++;
		input[2] = *src++;
		srclength -= 3;
    
		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
		output[3] = input[2] & 0x3f;
    
		if (datalength + 4 > targsize) {
			return (-1);
    }
    
		target[datalength++] = b64Alphabet[output[0]];
		target[datalength++] = b64Alphabet[output[1]];
		target[datalength++] = b64Alphabet[output[2]];
		target[datalength++] = b64Alphabet[output[3]];
	}
  
	/* Now we worry about padding. */
	if (0 != srclength) {
		/* Get what's left. */
		input[0] = input[1] = input[2] = '\0';
		for (i = 0; i < srclength; i++) {
			input[i] = *src++;
    }
    
		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
    
		if (datalength + 4 > targsize) {
			return (-1);
    }
    
		target[datalength++] = b64Alphabet[output[0]];
		target[datalength++] = b64Alphabet[output[1]];
		if (srclength == 1) {
			target[datalength++] = '=';
    } else {
			target[datalength++] = b64Alphabet[output[2]];
    }
		target[datalength++] = '=';
	}
	if (datalength >= targsize) {
		return (-1);
  }
	target[datalength] = '\0';
	return (datalength);
}

#ifdef DEBUG
void WebSocketClient::setDebug(Stream *serial) {
  _debug = serial;
  debug(F("Websockets Debugging On!"));
}

void WebSocketClient::debug(const __FlashStringHelper *fmt, ... ){
  if(_debug == NULL) {
    return;
  }
  char tmp[128]; // resulting string limited to 128 chars
  va_list args;
  va_start(args, fmt);
  vsnprintf_P(tmp, 128, (const char*)fmt, args);
  va_end(args);
  _debug->println(tmp);
}

#else
void WebSocketClient::setDebug(Stream *serial) {
  serial->println(F("uncomment #define DEBUG to enable debugging"));
}
#endif

