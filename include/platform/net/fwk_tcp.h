/*
 * Hardware Abstraction Layer Net Interface
 *
 * File Name:   fwk_tcp.h
 * Author:      Yang Yujun
 * E-mail:      <yujiantianhu@163.com>
 * Created on:  2024.12.07
 *
 * Copyright (c) 2023   Yang Yujun <yujiantianhu@163.com>
 *
 */

#ifndef __FWK_TCP_H_
#define __FWK_TCP_H_

/*!< The includes */
#include <platform/net/fwk_if.h>
#include <platform/net/fwk_ip.h>

/*!< The defines */
#define NET_TCP_HDR_LEN                         (20)
#define NET_TCP_FAKE_HDR_LEN                    (12)

struct fwk_tcp_fakehdr
{
    kuint32_t saddr;                                        /*!< source ip address */
    kuint32_t daddr;                                        /*!< destination IP address */
    kuint8_t zero;                                          /*!< 0 */
    kuint8_t proto;                                         /*!< NET_IP_PROTO_TCP */
    kuint16_t len;                                          /*!< NET_TCP_HDR_LEN + data_len */
    
} __packed;

struct fwk_tcp_hdr
{
    kuint16_t src_port;
    kuint16_t dst_port;
    kuint32_t seq_no;
    kuint32_t ack_no;

    kuint16_t hdr_len : 4;
    kuint16_t rsvd : 6;
    kuint16_t urg : 1;
    kuint16_t ack : 1;
    kuint16_t psh : 1;
    kuint16_t rst : 1;
    kuint16_t syn : 1;
    kuint16_t fin : 1;

    kuint16_t win_len;
    kuint16_t check_sum;
    kuint16_t urg_p;
    
} __packed;

#endif /*!< __FWK_TCP_H_ */
