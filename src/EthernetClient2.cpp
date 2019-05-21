/* Copyright 2018 Paul Stoffregen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Arduino.h>
#include "Ethernet.h"
#include "Dns2.h"
#include "utility/w5100.h"

int EthernetClient2::connect(const char * host, uint16_t port)
{
	DNSClient2 dns; // Look up the host first
	IP6Address remote_addr;

	if (sockindex < MAX_SOCK_NUM) {
		if (Ethernet2.socketStatus(sockindex) != SnSR::CLOSED) {
			Ethernet2.socketDisconnect(sockindex); // TODO: should we call stop()?
		}
		sockindex = MAX_SOCK_NUM;
	}
	dns.begin(Ethernet2.dnsServerIP());
	if (!dns.getHostByName(host, remote_addr)) return 0; // TODO: use _timeout
	return connect(remote_addr, port);
}

int EthernetClient2::connect(IP6Address ip, uint16_t port)
{
	if (sockindex < MAX_SOCK_NUM) {
		if (Ethernet2.socketStatus(sockindex) != SnSR::CLOSED) {
			Ethernet2.socketDisconnect(sockindex); // TODO: should we call stop()?
		}
		sockindex = MAX_SOCK_NUM;
	}
#if defined(ESP8266) || defined(ESP32)
	if (ip == IP6Address((uint32_t)0) || ip == IP6Address(0xFFFFFFFFul)) return 0;
#else
	if (ip == IP6Address(0ul) || ip == IP6Address(0xFFFFFFFFul)) return 0;
#endif
	sockindex = Ethernet2.socketBegin(SnMR::TCP6, 0);
	if (sockindex >= MAX_SOCK_NUM) return 0;
	Ethernet2.socketConnect(sockindex, rawIPAddress(ip), port);
	uint32_t start = millis();
	while (1) {
		uint8_t stat = Ethernet2.socketStatus(sockindex);
		if (stat == SnSR::ESTABLISHED) return 1;
		if (stat == SnSR::CLOSE_WAIT) return 1;
		if (stat == SnSR::CLOSED) return 0;
		if (millis() - start > _timeout) break;
		delay(1);
	}
	Ethernet2.socketClose(sockindex);
	sockindex = MAX_SOCK_NUM;
	return 0;
}

int EthernetClient2::availableForWrite(void)
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	return Ethernet2.socketSendAvailable(sockindex);
}

size_t EthernetClient2::write(uint8_t b)
{
	return write(&b, 1);
}

size_t EthernetClient2::write(const uint8_t *buf, size_t size)
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	if (Ethernet2.socketSend(sockindex, buf, size)) return size;
	setWriteError();
	return 0;
}

int EthernetClient2::available()
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	return Ethernet2.socketRecvAvailable(sockindex);
	// TODO: do the Wiznet chips automatically retransmit TCP ACK
	// packets if they are lost by the network?  Someday this should
	// be checked by a man-in-the-middle test which discards certain
	// packets.  If ACKs aren't resent, we would need to check for
	// returning 0 here and after a timeout do another Sock_RECV
	// command to cause the Wiznet chip to resend the ACK packet.
}

int EthernetClient2::read(uint8_t *buf, size_t size)
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	return Ethernet2.socketRecv(sockindex, buf, size);
}

int EthernetClient2::peek()
{
	if (sockindex >= MAX_SOCK_NUM) return -1;
	if (!available()) return -1;
	return Ethernet2.socketPeek(sockindex);
}

int EthernetClient2::read()
{
	uint8_t b;
	if (Ethernet2.socketRecv(sockindex, &b, 1) > 0) return b;
	return -1;
}

void EthernetClient2::flush()
{
	while (sockindex < MAX_SOCK_NUM) {
		uint8_t stat = Ethernet2.socketStatus(sockindex);
		if (stat != SnSR::ESTABLISHED && stat != SnSR::CLOSE_WAIT) return;
		if (Ethernet2.socketSendAvailable(sockindex) >= W5100.SSIZE) return;
	}
}

void EthernetClient2::stop()
{
	if (sockindex >= MAX_SOCK_NUM) return;

	// attempt to close the connection gracefully (send a FIN to other side)
	Ethernet2.socketDisconnect(sockindex);
	unsigned long start = millis();

	// wait up to a second for the connection to close
	do {
		if (Ethernet2.socketStatus(sockindex) == SnSR::CLOSED) {
			sockindex = MAX_SOCK_NUM;
			return; // exit the loop
		}
		delay(1);
	} while (millis() - start < _timeout);

	// if it hasn't closed, close it forcefully
	Ethernet2.socketClose(sockindex);
	sockindex = MAX_SOCK_NUM;
}

uint8_t EthernetClient2::connected()
{
	if (sockindex >= MAX_SOCK_NUM) return 0;

	uint8_t s = Ethernet2.socketStatus(sockindex);
	return !(s == SnSR::LISTEN || s == SnSR::CLOSED || s == SnSR::FIN_WAIT ||
		(s == SnSR::CLOSE_WAIT && !available()));
}

uint8_t EthernetClient2::status()
{
	if (sockindex >= MAX_SOCK_NUM) return SnSR::CLOSED;
	return Ethernet2.socketStatus(sockindex);
}

// the next function allows us to use the client returned by
// EthernetServer::available() as the condition in an if-statement.
bool EthernetClient2::operator==(const EthernetClient2& rhs)
{
	if (sockindex != rhs.sockindex) return false;
	if (sockindex >= MAX_SOCK_NUM) return false;
	if (rhs.sockindex >= MAX_SOCK_NUM) return false;
	return true;
}

// https://github.com/per1234/EthernetMod
// from: https://github.com/ntruchsess/Arduino-1/commit/937bce1a0bb2567f6d03b15df79525569377dabd
uint16_t EthernetClient2::localPort()
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	uint16_t port;
	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
	port = W5100.readSnPORT(sockindex);
	SPI.endTransaction();
	return port;
}

// https://github.com/per1234/EthernetMod
// returns the remote IP address: http://forum.arduino.cc/index.php?topic=82416.0
IP6Address EthernetClient2::remoteIP()
{
	if (sockindex >= MAX_SOCK_NUM) return IP6Address((uint32_t)0);
	uint8_t remoteIParray[16];
	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);

	if(W5100.readSnMR(sockindex) == W6100_SnMR_TCPD) {

		// TCP DUAL
		if((W5100.readSnESR(sockindex) & W6100_SnESR_TCP6) == W6100_SnESR_TCP6)	{
			W5100.readSnDIP6R(sockindex, remoteIParray);
		} else {
			W5100.readSnDIPR(sockindex, remoteIParray);
			memset(&remoteIParray[4], 0, 16-4);
		}

	} else if(W5100.readSnMR(sockindex) == W6100_SnMR_TCP4) {

		// TCP IPv4
		W5100.readSnDIPR(sockindex, remoteIParray);
		memset(&remoteIParray[4], 0, 16-4);

	} else {
		
		// TCP IPv6
		W5100.readSnDIP6R(sockindex, remoteIParray);
	}

	SPI.endTransaction();
	return IP6Address(remoteIParray);
}

// https://github.com/per1234/EthernetMod
// from: https://github.com/ntruchsess/Arduino-1/commit/ca37de4ba4ecbdb941f14ac1fe7dd40f3008af75
uint16_t EthernetClient2::remotePort()
{
	if (sockindex >= MAX_SOCK_NUM) return 0;
	uint16_t port;
	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
	port = W5100.readSnDPORT(sockindex);
	SPI.endTransaction();
	return port;
}

uint8_t EthernetClient2::IPVis()
{
	uint8_t ipv;

	if (sockindex >= MAX_SOCK_NUM) return IP6Address((uint32_t)0);
	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);

	if(W5100.readSnMR(sockindex) == W6100_SnMR_TCPD) {

		// TCP DUAL
		if((W5100.readSnESR(sockindex) & W6100_SnESR_TCP6) == W6100_SnESR_TCP6)	{
			// IPv6
			ipv = 6;
		} else {
			// IPv4
			ipv = 4;
		}

	} else if(W5100.readSnMR(sockindex) == W6100_SnMR_TCP4) {

		// TCP IPv4
		ipv = 4;

	} else {
		
		// TCP IPv6
		ipv = 6;
	}

	SPI.endTransaction();
	return ipv;
}

