/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2019-07-23     WillianChan  DIY Demo2(Second week mission)
 * 2019-07-30     WillianChan  DIY Demo3(Third week mission)
 * 2019-08-05     WillianChan  DIY Demo4(Fourth week mission)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include <string.h>
#include <rtdbg.h>
#include "drv_spi.h"
#include "nrf24l01.h"
#include "onenet.h"

#define LED0_PIN            GET_PIN(E, 7)
#define NRF24L01_CE_PIN     GET_PIN(D, 4)
#define NRF24_IRQ_PIN       GET_PIN(D, 3)
#define NRF24L01_SPI_DEVICE "spi20"
#define WRITE_EVENT_P0      (0x01U << 0)            /* ����Ȥ���¼� for nrf24l01 pipe0 data */
#define WRITE_EVENT_P1      (0x01U << 1)            /* ����Ȥ���¼� for nrf24l01 pipe1 data */
#define RINGBUFFERSIZE      (1024)                  /* ringbuffer��������С1KB */
#define THRESHOLD           (RINGBUFFERSIZE / 2)    /* ringbuffer��������ֵ */
#define MB_LEN              (4)
#define MP_LEN              MB_LEN
#define MP_BLOCK_SIZE       RT_ALIGN(sizeof(struct recvdata), sizeof(intptr_t))

static rt_event_t recvdata_event;                   /* �¼��� */
static rt_sem_t mqttinit_sem;                       /* �ź��� */
static rt_mailbox_t tmp_msg_mb;                     /* ���� */
static rt_mp_t tmp_msg_mp;                          /* �ڴ�� */
static struct rt_ringbuffer *recvdatabuf_p0;        /* ringbuffer for nrf24l01 pipe0 data */
static struct rt_ringbuffer *recvdatabuf_p1;        /* ringbuffer for nrf24l01 pipe1 data */
static rt_thread_t onenet_upload_data_thread;       /* onenet�ϴ����ݵ��̵߳ľ�� */
static rt_thread_t onenet_mqtt_init_thread;         /* mqtt��ʼ�����̵߳ľ�� */
static FILE *recvdatafile_p0 = RT_NULL;             /* �ļ�ָ�� for nrf24l01 pipe0 data */
static FILE *recvdatafile_p1 = RT_NULL;             /* �ļ�ָ�� for nrf24l01 pipe1 data */

struct recvdata                                     /* ����һ���ṹ���Ž��յ����� */
{
    int timestamp_p0;                               /* ʱ��� for nrf24l01 pipe0 data */
    float temperature_p0;                           /* �¶�ֵ for nrf24l01 pipe0 data */
    int timestamp_p1;                               /* ʱ��� for nrf24l01 pipe1 data */
    float temperature_p1;                           /* �¶�ֵ for nrf24l01 pipe1 data */
};

static void nrf24l01_receive_entry(void *parameter)
{
    struct recvdata buf;
    struct recvdata *buf_mp;
    static char str_data_p0[64];
    static char str_data_p1[64];
    struct hal_nrf24l01_port_cfg halcfg;
    nrf24_cfg_t cfg;

    nrf24_default_param(&cfg);
    halcfg.ce_pin = NRF24L01_CE_PIN;
    halcfg.spi_device_name = NRF24L01_SPI_DEVICE;
    cfg.role = ROLE_PRX;
    cfg.ud = &halcfg;
    cfg.use_irq = 0;
    nrf24_init(&cfg);

    while (1)
    {
        if (!rx_pipe_num_choose())
        {
            /* ͨ��sscnaf�����յ��ķ��ͽڵ�1������ */
            if(sscanf((char *)RxBuf_P0, "%d,+%f", &buf.timestamp_p0, &buf.temperature_p0) != 2)
            {
                /* ͨ��sscnaf�����յ��ķ��ͽڵ�1���� */
                if(sscanf((char *)RxBuf_P0, "%d,-%f", &buf.timestamp_p0, &buf.temperature_p0) != 2)
                {
                    continue;
                }
                buf.temperature_p0 = -buf.temperature_p0;
            }
            sprintf(str_data_p0, "%d,%f\n", buf.timestamp_p0, buf.temperature_p0);
            /* ͨ��sscnaf�����յ��ķ��ͽڵ�2������ */
            if(sscanf((char *)RxBuf_P1, "%d,+%f", &buf.timestamp_p1, &buf.temperature_p1) != 2)
            {
                /* ͨ��sscnaf�����յ��ķ��ͽڵ�2������ */
                if(sscanf((char *)RxBuf_P1, "%d,-%f", &buf.timestamp_p1, &buf.temperature_p1) != 2)
                {
                    continue;
                }
                buf.temperature_p1 = -buf.temperature_p1;
            }
            sprintf(str_data_p1, "%d,%f\n", buf.timestamp_p1, buf.temperature_p1);
            /* �����ݴ�ŵ�ringbuffer�� */
            rt_ringbuffer_put(recvdatabuf_p0, (rt_uint8_t *)str_data_p0, strlen(str_data_p0));
            rt_ringbuffer_put(recvdatabuf_p1, (rt_uint8_t *)str_data_p1, strlen(str_data_p1));
            /* �յ����ݣ��������ݴ�ŵ�ringbuffer��󣬲ŷ����¼� */
            rt_event_send(recvdata_event, WRITE_EVENT_P0);
            rt_event_send(recvdata_event, WRITE_EVENT_P1);
            
            /* ����һ���ڴ� Ҫ���ڴ������ �͹���ȴ� */
            buf_mp = rt_mp_alloc(tmp_msg_mp, RT_WAITING_FOREVER);
            buf_mp->timestamp_p0 = buf.timestamp_p0;
            buf_mp->temperature_p0 = buf.temperature_p0;
            buf_mp->timestamp_p1 = buf.timestamp_p1;
            buf_mp->temperature_p1 = buf.temperature_p1;
            rt_mb_send(tmp_msg_mb, (rt_ubase_t)buf_mp);
            buf_mp = NULL;
        }
        rt_thread_mdelay(500);
    }
}

static void save_recv_p0_data_entry(void *parameter)
{
    rt_uint32_t set;
    static int writebuffer[1024];
    rt_size_t size;
    
    while (1)
    {
        /* ���ո���Ȥ���¼�WRITE_EVENT_P0�������õȴ���ʽȥ���� */
        if (rt_event_recv(recvdata_event, WRITE_EVENT_P0, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &set) != RT_EOK)
        {
            continue;
        }
        
        do
        {
            /* ���ո���Ȥ���¼�WRITE_EVENT_P0����1000ms��ʱ��ʽ���� */
            if (rt_event_recv(recvdata_event, WRITE_EVENT_P0, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(1000), &set) == RT_EOK)
            {
                /* �ж�д������ݴ�С��û�������õ�ringbuffer����ֵ */
                if (rt_ringbuffer_data_len(recvdatabuf_p0) > THRESHOLD)
                {
                    /* ����ֵ��ֱ��д���� */
                    recvdatafile_p0 = fopen("recvdata_p0.csv", "ab+");
                    if (recvdatafile_p0 != RT_NULL)
                    {
                        while(rt_ringbuffer_data_len(recvdatabuf_p0))
                        {
                            size = rt_ringbuffer_get(recvdatabuf_p0, (rt_uint8_t *)writebuffer, THRESHOLD);
                            fwrite(writebuffer, 1, size, recvdatafile_p0);
                        }
                        fclose(recvdatafile_p0);
                    }
                }
                /* ��ֵû���ͼ������ո���Ȥ���¼�WRITE_EVENT_P0����1000ms��ʱ��ʽ���� */
                continue;
            }
            /* 1000ms���ˣ���û���յ�����Ȥ���¼�����ʱ�򲻹ܵ�û����ֵ��ֱ��д */
            recvdatafile_p0 = fopen("recvdata_p0.csv", "ab+");
            if (recvdatafile_p0 != RT_NULL)
            {
                while(rt_ringbuffer_data_len(recvdatabuf_p0))
                {
                    size = rt_ringbuffer_get(recvdatabuf_p0, (rt_uint8_t *)writebuffer, THRESHOLD);
                    fwrite(writebuffer, 1, size, recvdatafile_p0);
                }
                fclose(recvdatafile_p0);
            }
        } while (0);
    }
}

static void save_recv_p1_data_entry(void *parameter)
{
    rt_uint32_t set;
    static int writebuffer[1024];
    rt_size_t size;
    
    while (1)
    {
        /* ���ո���Ȥ���¼�WRITE_EVENT_P1�������õȴ���ʽȥ���� */
        if (rt_event_recv(recvdata_event, WRITE_EVENT_P1, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &set) != RT_EOK)
        {
            continue;
        }
        
        do
        {
            /* ���ո���Ȥ���¼�WRITE_EVENT_P1����1000ms��ʱ��ʽ���� */
            if (rt_event_recv(recvdata_event, WRITE_EVENT_P1, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(1000), &set) == RT_EOK)
            {
                /* �ж�д������ݴ�С��û�������õ�ringbuffer����ֵ */
                if (rt_ringbuffer_data_len(recvdatabuf_p1) > THRESHOLD)
                {
                    /* ����ֵ��ֱ��д���� */
                    recvdatafile_p1 = fopen("recvdata_p1.csv", "ab+");
                    if (recvdatafile_p1 != RT_NULL)
                    {
                        while(rt_ringbuffer_data_len(recvdatabuf_p1))
                        {
                            size = rt_ringbuffer_get(recvdatabuf_p1, (rt_uint8_t *)writebuffer, THRESHOLD);
                            fwrite(writebuffer, 1, size, recvdatafile_p1);
                        }
                        fclose(recvdatafile_p1);
                    }
                }
                /* ��ֵû���ͼ������ո���Ȥ���¼�WRITE_EVENT_P1����1000ms��ʱ��ʽ���� */
                continue;
            }
            /* 1000ms���ˣ���û���յ�����Ȥ���¼�����ʱ�򲻹ܵ�û����ֵ��ֱ��д */
            recvdatafile_p1 = fopen("recvdata_p1.csv", "ab+");
            if (recvdatafile_p1 != RT_NULL)
            {
                while(rt_ringbuffer_data_len(recvdatabuf_p1))
                {
                    size = rt_ringbuffer_get(recvdatabuf_p1, (rt_uint8_t *)writebuffer, THRESHOLD);
                    fwrite(writebuffer, 1, size, recvdatafile_p1);
                }
                fclose(recvdatafile_p1);
            }
        } while (0);
    }
}

static void onenet_mqtt_init_entry(void *parameter)
{
    uint8_t onenet_mqtt_init_failed_times;
    
    /* mqtt��ʼ�� */
    while (onenet_mqtt_init())
    {
        rt_thread_mdelay(100);
        LOG_E("onenet mqtt init failed %d times", onenet_mqtt_init_failed_times++);
    }
    /* mqtt��ʼ���ɹ�֮���ͷ��ź�����֪onenet_upload_data_thread�߳̿����ϴ������� */
    rt_sem_release(mqttinit_sem);
}

static void onenet_upload_data_entry(void *parameter)
{
    struct recvdata *buf_mp;
    
    /* ���õȴ���ʽ�����ź��������ղ��������̻߳�һֱ���� */
    rt_sem_take(mqttinit_sem, RT_WAITING_FOREVER);
    /* �����ò�������ź����ˣ�����ɾ���ˣ�������Դ */
    rt_sem_delete(mqttinit_sem);
    
    while (1)
    {
        if (rt_mb_recv(tmp_msg_mb, (rt_ubase_t*)&buf_mp, RT_WAITING_FOREVER) == RT_EOK)
        {
            /* 500ms�ϴ�һ������ */
            rt_thread_delay(rt_tick_from_millisecond(500));
            
            /* �ϴ����ͽڵ�1�����ݵ�OneNet��������������������temperature_p0 */
            if (onenet_mqtt_upload_digit("temperature_p0", buf_mp->temperature_p0) != RT_EOK)
            {
                rt_kprintf("upload temperature_p0 has an error, try again\n");
            }
            else
            {
                printf("onenet upload OK >>> temp_p0:%f\n", buf_mp->temperature_p0);
            }
            
            /* �ϴ����ͽڵ�2�����ݵ�OneNet��������������������temperature_p1 */
            if (onenet_mqtt_upload_digit("temperature_p1", buf_mp->temperature_p1) != RT_EOK)
            {
                rt_kprintf("upload temperature_p1 has an error, try again\n");
            }
            else
            {
                printf("onenet upload OK >>> temp_p1:%f\n", buf_mp->temperature_p1);
            }
            
            rt_kputs("\n\n");
            
            rt_mp_free(buf_mp); /* �ͷ��ڴ�� */
            buf_mp = RT_NULL;   /* �����Ҫ�� */
        }
    }
}

static void led_shine_entry(void *parameter)
{
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
    
    while (1)
    {
        rt_pin_write(LED0_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED0_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}

int main(void)
{
    rt_thread_t nrf24l01_thread, led_thread, DFS_thread_p0, DFS_thread_p1;
    
    mqttinit_sem = rt_sem_create("mqtt_sem", RT_NULL, RT_IPC_FLAG_FIFO);
    RT_ASSERT(mqttinit_sem);
    recvdata_event = rt_event_create("temp_evt0", RT_IPC_FLAG_FIFO);
    RT_ASSERT(recvdata_event);
    recvdatabuf_p0 = rt_ringbuffer_create(RINGBUFFERSIZE);
    RT_ASSERT(recvdatabuf_p0);
    recvdatabuf_p1 = rt_ringbuffer_create(RINGBUFFERSIZE);
    RT_ASSERT(recvdatabuf_p1);
    tmp_msg_mb = rt_mb_create("temp_mb0", MB_LEN, RT_IPC_FLAG_FIFO);
    RT_ASSERT(tmp_msg_mb);
    tmp_msg_mp = rt_mp_create("temp_mp0", MP_LEN, MP_BLOCK_SIZE);
    RT_ASSERT(tmp_msg_mp);
    
    nrf24l01_thread  = rt_thread_create("nrfrecv", nrf24l01_receive_entry, RT_NULL,
                                        1024, RT_THREAD_PRIORITY_MAX / 2, 20);
    if (nrf24l01_thread != RT_NULL)
    {
        rt_thread_startup(nrf24l01_thread);
    }
    
    DFS_thread_p0 = rt_thread_create("DFSsaveP0", save_recv_p0_data_entry, RT_NULL,
                                  1024, RT_THREAD_PRIORITY_MAX / 2 - 1, 20);
    if (DFS_thread_p0 != RT_NULL)
    {
        rt_thread_startup(DFS_thread_p0);
    }
    
    DFS_thread_p1 = rt_thread_create("DFSsaveP1", save_recv_p1_data_entry, RT_NULL,
                                  1024, RT_THREAD_PRIORITY_MAX / 2 - 1, 20);
    if (DFS_thread_p1 != RT_NULL)
    {
        rt_thread_startup(DFS_thread_p1);
    }
    
    onenet_mqtt_init_thread = rt_thread_create("mqttinit", onenet_mqtt_init_entry, RT_NULL,
                                               1024, RT_THREAD_PRIORITY_MAX / 2 - 2, 20);
    if (onenet_mqtt_init_thread != RT_NULL)
    {
        rt_thread_startup(onenet_mqtt_init_thread);
    }
    
    onenet_upload_data_thread = rt_thread_create("uploaddata", onenet_upload_data_entry, RT_NULL,
                                                 1024, RT_THREAD_PRIORITY_MAX / 2 - 2, 20);
    if (onenet_upload_data_thread != RT_NULL)
    {
        rt_thread_startup(onenet_upload_data_thread);
    }

    /* һ�����޴��ڸе��߳� */
    led_thread = rt_thread_create("ledshine", led_shine_entry, RT_NULL,
                                  192, RT_THREAD_PRIORITY_MAX - 2, 20);
    if (led_thread != RT_NULL)
    {
        rt_thread_startup(led_thread);
    }

    return RT_EOK;
}

static int rt_hw_nrf24l01_init(void)
{
    rt_hw_spi_device_attach("spi2", "spi20", GPIOD, GPIO_PIN_5);
    
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(rt_hw_nrf24l01_init);
