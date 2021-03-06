/*
 * Copyright (c) 2016 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "mbed.h"
#include "nanostack/socket_api.h"
#include "mesh_led_control_example.h"
#include "common_functions.h"
#include "ip6string.h"
#include "mbed-trace/mbed_trace.h"

static void init_socket();
static void handle_socket();
static void receive();
static void my_button_isr();
static void send_message();
static void blink();

// mesh local multicast to all nodes
#define multicast_addr_str "ff02::1"
#define TRACE_GROUP "example"
#define UDP_PORT 1234
#define MESSAGE_WAIT_TIMEOUT (30.0)

DigitalOut led_1(MBED_CONF_APP_LED, 1);
InterruptIn my_button(MBED_CONF_APP_BUTTON);
DigitalOut output(D3, 1);
Timeout messageTimeout;

NetworkInterface * network_if;
UDPSocket* my_socket;
// queue for sending messages from button press.
EventQueue queue;
// for LED blinking
Ticker ticker;

uint8_t multi_cast_addr[16] = {0};
uint8_t receive_buffer[5];
static const char buffer_on[2] = {'o','n'};
static const char buffer_off[3] = {'o','f','f'};
// how many hops the multicast message can go
static const int16_t multicast_hops = 10;
bool button_status = 0;

void start_mesh_led_control_example(NetworkInterface * interface){
    tr_debug("start_mesh_led_control_example()");
    MBED_ASSERT(MBED_CONF_APP_LED != NC);
    MBED_ASSERT(MBED_CONF_APP_BUTTON != NC);

    network_if = interface;
    stoip6(multicast_addr_str, strlen(multicast_addr_str), multi_cast_addr);
    init_socket();
}

static void messageTimeoutCallback()
{
    send_message();
}

static void blink() {
    led_1 = !led_1;
}

void start_blinking() {
    ticker.attach(blink, 1.0);
}

void cancel_blinking() {
    ticker.detach();
    led_1=1;
}

static void send_message() {
    tr_debug("send msg %d", button_status);
    
    SocketAddress send_sockAddr(multi_cast_addr, NSAPI_IPv6, UDP_PORT);
    if (button_status) {
        led_1 = 0;
        output = 0;
        my_socket->sendto(send_sockAddr, buffer_on, 2);
    }
    else {
        led_1 = 1;
        output = 1;
        my_socket->sendto(send_sockAddr, buffer_off, 3);
    }
}

// As this comes from isr, we cannot use printing or network functions directly from here.
static void my_button_isr() {
    button_status = !button_status;
    queue.call(send_message);
}

static void receive() {
    // Read data from the socket
    SocketAddress source_addr;
    memset(receive_buffer, 0, sizeof(receive_buffer));
    bool something_in_socket=true;
    // read all messages
    while (something_in_socket) {
        int length = my_socket->recvfrom(&source_addr, receive_buffer, sizeof(receive_buffer) - 1);
        if (length > 0) {
            int timeout_value = MESSAGE_WAIT_TIMEOUT;
            tr_debug("Packet from %s\n", source_addr.get_ip_address());
            timeout_value += rand() % 30;
            tr_debug("Advertisiment after %d seconds", timeout_value);
            messageTimeout.detach();
            messageTimeout.attach(&messageTimeoutCallback, timeout_value);
            // Handle command - "on", "off"
            if (strcmp((char*)receive_buffer, "on") == 0) {
                tr_debug("Turning led on\n");
                led_1 = 0;
                button_status=1;
                output = 0;
            }
            if (strcmp((char*)receive_buffer, "off") == 0) {
                tr_debug("Turning led off\n");
                led_1 = 1;
                button_status=0;
                output = 1;
            }
        }
        else if (length!=NSAPI_ERROR_WOULD_BLOCK) {
            tr_error("Error happened when receiving %d\n", length);
            something_in_socket=false;
        }
        else {
            // there was nothing to read.
            something_in_socket=false;
        }
    }    
}

static void handle_socket() {
    // call-back might come from ISR
    queue.call(receive);
}

static void init_socket()
{
    my_socket = new UDPSocket(network_if);
    my_socket->set_blocking(false);
    my_socket->bind(UDP_PORT);    
    my_socket->setsockopt(SOCKET_IPPROTO_IPV6, SOCKET_IPV6_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
    if (MBED_CONF_APP_BUTTON != NC) {
        my_button.fall(&my_button_isr);
        my_button.mode(PullUp);
    }
    //let's register the call-back function.
    //If something happens in socket (packets in or out), the call-back is called.
    my_socket->sigio(callback(handle_socket));
    // dispatch forever
    queue.dispatch();
}
