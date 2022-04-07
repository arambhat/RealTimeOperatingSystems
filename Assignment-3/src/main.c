/*
 * Copyright (c) 2020 Daniel Veilleux
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <string.h>
#include <zephyr.h>
#include <device.h>
#include <drivers/sensor.h>
#include <sys/byteorder.h>
#include <stdio.h>
#include <sys/__assert.h>
#include <shell/shell.h>
#include <linker/sections.h>
/* CoAP server headers*/
#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);
#include <net/socket.h>
#include <net/net_mgmt.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_ip.h>
#include <net/udp.h>
#include <net/coap.h>
#include <net/coap_link_format.h>
#include "net_private.h"
/* GPIO headers */
#include <drivers/gpio.h>

/* Device Tree configurations */
#define HCSR_0 DT_PROP(DT_NODELABEL(distance_sensor0), label)
#define HCSR_1 DT_PROP(DT_NODELABEL(distance_sensor1), label)

#define LED_RED DT_NODELABEL(r_led)

#define LED0	DT_GPIO_LABEL(LED_RED, gpios)
#define PIN0	DT_GPIO_PIN(LED_RED, gpios)
#define FLAGS0	DT_GPIO_FLAGS(LED_RED, gpios)

#define LED_GREEN DT_NODELABEL(g_led)

#define LED1	DT_GPIO_LABEL(LED_GREEN, gpios)
#define PIN1	DT_GPIO_PIN(LED_GREEN, gpios)
#define FLAGS1	DT_GPIO_FLAGS(LED_GREEN, gpios)

#define LED_BLUE DT_NODELABEL(b_led)

#define LED2	DT_GPIO_LABEL(LED_BLUE, gpios)
#define PIN2	DT_GPIO_PIN(LED_BLUE, gpios)
#define FLAGS2	DT_GPIO_FLAGS(LED_BLUE, gpios)
/* End of device tree configurations*/

#define MAX_COAP_MSG_LEN 256
#define MY_COAP_PORT 5683
#define NUM_OBSERVERS 3
#define NUM_RESOURCES 2 // Only distance sensors are observing resourses.
#define NUM_PENDINGS 3
#define LED_ON 1
#define LED_OFF 0

/* CoAP socket fd */
static int sock;
static struct coap_observer observers[NUM_OBSERVERS];
static struct coap_pending pendings[NUM_PENDINGS];
static struct coap_resource *resources_cache[NUM_RESOURCES];
static struct k_work_delayable retransmit_work;

/* Distance Sensor related structs*/
const struct device *hcsr_0_dev;
const struct device *hcsr_1_dev;
const struct device *ledrg_dev;
const struct device *ledb_dev;
struct k_mutex distanceMtx, samplingMtx;
static struct k_work_delayable distance0_work, distance1_work;
/* Cached distance values */
struct sensor_value gDistance0, gDistance1;

/* Last updated distance to client*/
struct sensor_value clientDistance0, clientDistance1;

uint8_t gSamplingPeriod;

struct led_state {
    bool isLedrOn;
    bool isLedgOn;
    bool isLedbOn;
} ledState;

/* Distance measuring function */
static void measure(struct device *dev, struct sensor_value *distance)
{
    int ret;

    ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ALL);
    switch (ret) {
    case 0:
        ret = sensor_channel_get(dev, SENSOR_CHAN_DISTANCE, distance);
        if (ret) {
            LOG_ERR("sensor_channel_get failed ret %d", ret);
            return;
        }
        LOG_INF("%s: %d.%03dinch", dev->name, distance->val1, (distance->val2 / 1000));
        break;
    case -EIO:
        LOG_WRN("%s: Could not read device", dev->name);
        break;
    default:
        LOG_ERR("Error when reading device: %s", dev->name);
        break;
    }
    return;
}

/* Distance measuring function work handlers for two hc-sr04 sensors */
static void updateDistance(struct k_work *work) {
    struct sensor_value distance0, distance1;
    bool notify[NUM_RESOURCES] = {false, false}; 
    uint8_t samplingRate;

    /* Updating the distance sensor 1 in global cache */
    k_mutex_lock(&distanceMtx, K_FOREVER);
    /* Measure the sensor value */
    measure(hcsr_0_dev, &distance0);
    // As val2 comprises decimal values
    if ((clientDistance0.val2 - distance0.val2) > 5) {
        notify[0] = true;
    }
    gDistance0.val1 = distance0.val1;
    gDistance0.val2 = distance0.val2;
    LOG_DBG("Updated the distance1 values in cache");
    k_mutex_unlock(&distanceMtx);
    
    /* Adding a sleep to avoid errors due to simultaneous distance measurements */
    k_msleep(5);

    /* Updating the distance sensor 2 in global cache */
    k_mutex_lock(&distanceMtx, K_FOREVER);
    /* Measure the sensor value */
    measure(hcsr_1_dev, &distance1);
    // As val2 comprises decimal values
    if ((clientDistance1.val2 - distance1.val2) > 5) {
        notify[1] = true;
    }
    gDistance0.val1 = distance1.val1;
    gDistance0.val2 = distance1.val2;
    LOG_DBG("Updated the distance1 values in cache");
    k_mutex_unlock(&distanceMtx);

	if (notify[0] && resources_cache[0]) {
		coap_resource_notify(resources_cache[0]);
	}
	if (notify[1] && resources_cache[1]) {
		coap_resource_notify(resources_cache[1]);
	}

    /* Fetching the latest sampling period from the cache */
    k_mutex_lock(&samplingMtx, K_FOREVER);
    samplingRate = gSamplingPeriod;
    k_mutex_unlock(&samplingMtx);

    /* This work item will be invoked at next sample period */
	k_work_reschedule(&distance0_work, K_MSEC(samplingRate));
}

static int start_coap_server(void)
{
	struct sockaddr_in addr;
	int r;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(MY_COAP_PORT);

	sock = socket(addr.sin_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

	r = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (r < 0) {
		LOG_ERR("Failed to bind UDP socket %d", errno);
		return -errno;
	}

	return 0;
}

static int send_coap_reply(struct coap_packet *cpkt,
			   const struct sockaddr *addr,
			   socklen_t addr_len)
{
	int r;

	net_hexdump("Response", cpkt->data, cpkt->offset);

	r = sendto(sock, cpkt->data, cpkt->offset, 0, addr, addr_len);
	if (r < 0) {
		LOG_ERR("Failed to send %d", errno);
		r = -errno;
	}

	return r;
}

static int well_known_core_get(struct coap_resource *resource,
			       struct coap_packet *request,
			       struct sockaddr *addr, socklen_t addr_len)
{
    LOG_DBG("well_known_core_get: Entered");
	struct coap_packet response;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_well_known_core_get(resource, request, &response,
				     data, MAX_COAP_MSG_LEN);
    LOG_DBG("Received the well known cores from CoAP");
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;
}

static int distance_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len) {
    LOG_DBG(" Distance_get handler not implemented yet !");
    return -1;
}

static int distance_period_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len) 
{
    struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
    int sampling_period;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

    code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");
    payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
        //net_hexdump("PUT Payload", payload, payload_len);
        /* Updating the sampling period in global cache */
        k_mutex_lock(&samplingMtx, K_FOREVER);
        sampling_period = atoi(payload);
        gSamplingPeriod = sampling_period;
        LOG_DBG("The received sampling rate is: %d", sampling_period);
        k_mutex_unlock(&samplingMtx);
	}

    if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

    /* Sending back an Ack to the client */
    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);
end:
	k_free(data);

	return r;

}

static int led_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len) 
{
    LOG_DBG("Entered led_put function");
    struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

    code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);
    char* resourceName = (char*)((struct coap_core_metadata*)resource->user_data)->user_data;
    LOG_DBG(" THE RESOURCE NAME IS : %s", resourceName);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");
    payload = coap_packet_get_payload(request, &payload_len);
    /* 
     * If payload exists, parsing it and setting the actual led levels accordingly 
     * Also updating the LED state cache internally for the GET requests.
     */
	if (payload) {
        int value = atoi(payload);
        LOG_INF("Payload is %d\n", (char*)value);
		//net_hexdump("PUT Payload", payload, payload_len);
        bool isLedOn = (value != 0); // Maps to true if the Ledstate is ON, else false.
        /* Setting the respective GPIO states */
        LOG_DBG("JUST BEFORE STRCMP");
        if (strcmp(resourceName, "ledr") ==  0) {
            LOG_DBG("JUST AFTER STRCMP");
            if (ledrg_dev){
                gpio_pin_set(ledrg_dev, PIN0, isLedOn);
            } else {
                LOG_DBG("ledrg_dev is NULL!!, pin set failed!!!");
            }
            LOG_DBG("JUST AFTER SETTING GPIO PIN");
            ledState.isLedrOn = isLedOn;
            LOG_DBG(" LED R is set to: %d", isLedOn);
        } else if (strcmp(resourceName, "ledg") ==  0) {
            if (ledrg_dev){
                gpio_pin_set(ledrg_dev, PIN1, isLedOn);
            } else {
                LOG_DBG("ledrg_dev is NULL!!, pin set failed!!!");
            }
            ledState.isLedgOn = isLedOn;
            LOG_DBG(" LED G is set to: %d", isLedOn);
        } else if (strcmp(resourceName, "ledb") ==  0) {
            if (ledb_dev){
                gpio_pin_set(ledb_dev, PIN2, isLedOn);
            } else {
                LOG_DBG("ledb_dev is NULL!!, pin set failed!!!");
            }
            ledState.isLedbOn = isLedOn;
            LOG_DBG(" LED B is set to: %d", isLedOn);
        } else {
            LOG_DBG(" Unexpected LED pin entered");
        }
	}

    /* Updating the type of response packet based on the type of request packet */
    if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

    /* Sending back an Ack to the client */
    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);
end:
	k_free(data);

	return r;
}

static int led_get(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len) 
{
    LOG_DBG("Entered led_get function");
    struct coap_packet response;
	uint8_t payload[40];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t *data;
    uint8_t status;
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);
    char* resourceName = (char*)((struct coap_core_metadata*)resource->user_data)->user_data;

	LOG_DBG("*******");
	LOG_DBG("type: %u code %u id %u", type, code, id);
	LOG_DBG("*******");

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}
    /* Fetching the requested LED status from cache */
    if (strcmp(resourceName, "ledr") ==  0) {
        if (ledState.isLedrOn) {
            status = 1;
            LOG_DBG("LEDR status: %d", status);
        } else {
            status = 0;
            LOG_DBG("LEDR status: %d", status);
        }
        r = snprintk((char *) payload, sizeof(payload),
		        "Code: %u\nMID: %u\n LEDR status: %u\n", type, code, status);
        if (r < 0) {
            goto end;
        }
    } else if (strcmp(resourceName, "ledg") ==  0) {
        if (ledState.isLedgOn) {
            status = 1;
        } else {
            status = 0;
        }
        r = snprintk((char *) payload, sizeof(payload),
		        "Code: %u\nMID: %u\n LEDG status: %u\n", type, code, status);
        if (r < 0) {
            goto end;
        }
    } else if (strcmp(resourceName, "ledb") ==  0) {
        if (ledState.isLedbOn) {
            status = 1;
        } else {
            status = 0;
        }
        r = snprintk((char *) payload, sizeof(payload),
		        "Code: %u\nMID: %u LEDB status: %u\n", type, code, status);
        if (r < 0) {
            goto end;
        }
    } else {
        LOG_DBG(" Unexpected LED pin entered");
    }
    /* Sending the response with LED status back to the client */
	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		goto end;
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		goto end;
	}
	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		goto end;
	}
	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;

}



static void distance_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
    LOG_DBG("distance_notify: Entered");
    //char* resourceName = (char*)((struct coap_core_metadata*)resource->user_data)->user_data;
	// send_notification_packet(&observer->addr,
	// 			 sizeof(observer->addr),
	// 			 resource->age, 0,
	// 			 observer->token, observer->tkl, false);
}

// static int send_notification_packet(const struct sockaddr *addr,
// 				    socklen_t addr_len,
// 				    uint16_t age, uint16_t id,
// 				    const uint8_t *token, uint8_t tkl,
// 				    bool is_response);

static void retransmit_request(struct k_work *work)
{
	struct coap_pending *pending;

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return;
	}

	if (!coap_pending_cycle(pending)) {
		k_free(pending->data);
		coap_pending_clear(pending);
		return;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));
}

static int create_pending_request(struct coap_packet *response,
				  const struct sockaddr *addr)
{
	struct coap_pending *pending;
	int r;

	pending = coap_pending_next_unused(pendings, NUM_PENDINGS);
	if (!pending) {
		return -ENOMEM;
	}

	r = coap_pending_init(pending, response, addr,
			      COAP_DEFAULT_MAX_RETRANSMIT);
	if (r < 0) {
		return -EINVAL;
	}

	coap_pending_cycle(pending);

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return 0;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));

	return 0;
}

static const char * const distance0_path[] = { "sensor", "hcsr_0", NULL };
static const char * const distance1_path[] = { "sensor", "hcsr_1", NULL };
static const char * const distance_sample_rate_path[] = { "sensor", "period", NULL };
static const char * const ledr_path[] = { "led", "led_r", NULL };
static const char * const ledg_path[] = { "led", "led_g", NULL };
static const char * const ledb_path[] = { "led", "led_b", NULL };

static struct coap_resource resources[] = {
	{ .get = well_known_core_get,
	  .path = COAP_WELL_KNOWN_CORE_PATH,
	},
    {
      .get = distance_get,
      .path = distance0_path,
      .user_data = &((struct coap_core_metadata) {
          .attributes = NULL,
          .user_data = (void*) HCSR_0,
      })
    },
    {
      .get = distance_get,
      .path = distance1_path,
      .user_data = &((struct coap_core_metadata) {
          .attributes = NULL,
          .user_data = (void*) HCSR_1,
      })
    },
    {
      .put = distance_period_put,
      .path = distance_sample_rate_path,
    },
    {
      .get = led_get,
      .put = led_put,
      .path = ledr_path,
      .user_data = &((struct coap_core_metadata) {
          .attributes = NULL,
          .user_data = (void*) "ledr",
      })
    },
    {
      .get = led_get,
      .put = led_put,
      .path = ledg_path,
      .user_data = &((struct coap_core_metadata) {
          .attributes = NULL,
          .user_data = (void*) "ledg",
      })
    },
    {
      .get = led_get,
      .put = led_put,
      .path = ledb_path,
      .user_data = &((struct coap_core_metadata) {
          .attributes = NULL,
          .user_data = (void*) "ledb",
      })
    },
	{ },
};

static struct coap_resource *find_resouce_by_observer(
		struct coap_resource *resources, struct coap_observer *o)
{
	struct coap_resource *r;

	for (r = resources; r && r->path; r++) {
		sys_snode_t *node;
        LOG_DBG("Resource being tested: %s", *r->path);

		SYS_SLIST_FOR_EACH_NODE(&r->observers, node) {
            LOG_DBG("SYS_NODE(observer) being iterated : %s", (char*)node);
			if (&o->list == node) {
                LOG_DBG("resource matched");
				return r;
			}
		}
	}

	return NULL;
}

static void process_coap_request(uint8_t *data, uint16_t data_len,
				 struct sockaddr *client_addr,
				 socklen_t client_addr_len)
{   
    LOG_DBG("process_coap_request");
	struct coap_packet request;
	struct coap_pending *pending;
	struct coap_option options[16] = { 0 };
	uint8_t opt_num = 16U;
	uint8_t type;
	int r;

	r = coap_packet_parse(&request, data, data_len, options, opt_num);
	if (r < 0) {
		LOG_ERR("Invalid data received (%d)\n", r);
		return;
	}
    LOG_DBG("The CoAP packet parsed");
	type = coap_header_get_type(&request);
    LOG_DBG("The CoAP header tyoe: %u", type);
	pending = coap_pending_received(&request, pendings, NUM_PENDINGS);
	if (!pending) {
        LOG_DBG("Pending CoAP requests not found");
		goto not_found;
	}
    LOG_DBG("Pending CoAP requests found !!!");
	/* Clear CoAP pending request */
	if (type == COAP_TYPE_ACK) {
        LOG_DBG("Pending CoAP requests are being cleared if type is ACK");
		k_free(pending->data);
		coap_pending_clear(pending);
	}
    if (type == COAP_TYPE_RESET) {
        LOG_DBG("Reset Received... Moving to not found section");
		goto not_found;
    }
	return;

not_found:
    LOG_DBG("Entered Not Found section");
	if (type == COAP_TYPE_RESET) {
        LOG_DBG("Reset Type Received");
		struct coap_resource *r;
		struct coap_observer *o;

		o = coap_find_observer_by_addr(observers, NUM_OBSERVERS,
					       client_addr);
		if (!o) {
			LOG_ERR("Observer not found\n");
			goto end;
		}
        LOG_DBG("Found the observer");
		r = find_resouce_by_observer(resources, o);
		if (!r) {
			LOG_ERR("Observer found but Resource not found\n");
			goto end;
		}
        LOG_DBG("Found the resource");
		coap_remove_observer(r, o);
        LOG_DBG("Observer %s removed", (char*)&o->list);
		return;
	}

end:
    LOG_DBG("Entered End section");
	r = coap_handle_request(&request, resources, options, opt_num,
				client_addr, client_addr_len);
	if (r < 0) {
		LOG_WRN("No handler for such request (%d)\n", r);
	}
}

static int process_client_request(void)
{
    LOG_DBG("process_client_request");
	int received;
	struct sockaddr client_addr;
	socklen_t client_addr_len;
	uint8_t request[MAX_COAP_MSG_LEN];

	do {
        LOG_DBG("Entered do-while loop");
		client_addr_len = sizeof(client_addr);
		received = recvfrom(sock, request, sizeof(request), 0,
				    &client_addr, &client_addr_len);
        LOG_DBG("The Client connected");
		if (received < 0) {
			LOG_ERR("Connection error %d", errno);
			return -errno;
		}

		process_coap_request(request, received, &client_addr,
				     client_addr_len);
	} while(true);

	return 0;
}

 /* DHCPv4 network interface handler */

static void dhcpEventMgmtHandler(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event,
		    struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].addr_type !=
							NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("Your address: %s",
			log_strdup(net_addr_ntop(AF_INET,
			    &iface->config.ip.ipv4->unicast[i].address.in_addr,
						  buf, sizeof(buf))));
		LOG_INF("Lease time: %u seconds",
			 iface->config.dhcpv4.lease_time);
		LOG_INF("Subnet: %s",
			log_strdup(net_addr_ntop(AF_INET,
				       &iface->config.ip.ipv4->netmask,
				       buf, sizeof(buf))));
		LOG_INF("Router: %s",
			log_strdup(net_addr_ntop(AF_INET,
						 &iface->config.ip.ipv4->gw,
						 buf, sizeof(buf))));
	}
}

void setupNetworkInterface(void) 
{
    struct net_if *iface;
    static struct net_mgmt_event_callback mgmt_cb;
    LOG_INF("Run dhcpv4 client");
    net_mgmt_init_event_callback(&mgmt_cb, dhcpEventMgmtHandler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);
    iface = net_if_get_default();

	net_dhcpv4_start(iface);

}

struct device* initializeDeviceStructure(const char *name) 
{   
    LOG_DBG("Entered Initialise device structure \n");
    const struct device* dev = device_get_binding(name);
    if (dev == NULL) {
        LOG_ERR("Failed to get dev binding");
        return;
    } else {
        LOG_DBG("Device binding successful %p\n", dev);
    }
return dev;
}

void configureLeds(void) {
    LOG_DBG("Entered configure LEDs");
    int ret;
    /* Configuring the LED GPIO states */
    ret=gpio_pin_configure(ledrg_dev, PIN0, GPIO_OUTPUT_ACTIVE | FLAGS0); 
	if(ret<0)
		LOG_DBG("error configuring gpio pin 11 \n");
    gpio_pin_set(ledrg_dev, PIN0, LED_OFF);

	ret=gpio_pin_configure(ledrg_dev, PIN1, GPIO_OUTPUT_ACTIVE | FLAGS1); 
	if(ret<0)
		LOG_DBG("error configuring gpio pin 10 \n");
    gpio_pin_set(ledrg_dev, PIN1, LED_OFF);

	ret=gpio_pin_configure(ledb_dev, PIN2, GPIO_OUTPUT_ACTIVE | FLAGS2); 
	if(ret<0)
		LOG_DBG("error configuring gpio pin 15 \n");
    gpio_pin_set(ledb_dev, PIN2, LED_OFF);

    /* Setting up the initial led status to OFF*/
    ledState.isLedrOn = false;
    ledState.isLedgOn = false;
    ledState.isLedbOn = false;
    LOG_DBG("The current LED state is: %d %d %d", ledState.isLedrOn, ledState.isLedgOn, ledState.isLedbOn);
return;
}

void main(void)
{

    setupNetworkInterface();
    //initializeDeviceStructure(hcsr_0_dev,HCSR_0);
    // initializeDeviceStructure(hcsr_1_dev,HCSR_1); commented for testing one sensor.
    // k_work_init_delayable(&distance0_work, updateDistance0);
    // k_work_init_delayable(&distance1_work, updateDistance1);
    // initializeDeviceStructure(hcsr_1_dev, HCSR_1);
    // since all gpio pins are in gpio1, just need to bind once
    ledrg_dev = initializeDeviceStructure(LED0);
    if (!ledrg_dev) {
        LOG_DBG("LED DEV IS STILL NULL");
    }
    ledb_dev = initializeDeviceStructure(LED2);
    if (!ledb_dev) {
        LOG_DBG("LED DEV IS STILL NULL");
    }
    configureLeds();


    // const struct device *dev;
    if (IS_ENABLED(CONFIG_LOG_BACKEND_RTT)) {
        /* Give RTT log time to be flushed before executing tests */
        k_sleep(K_MSEC(500));
    }

    /* Starting the CoAP server */
    LOG_DBG("Start CoAP-server sample");
    int r;
    r = start_coap_server();
	if (r < 0) {
		goto quit;
	}

	k_work_init_delayable(&retransmit_work, retransmit_request);

	while (1) {
		r = process_client_request();
		if (r < 0) {
			goto quit;
		}
	}
quit:
    LOG_INF("exiting");
}

