#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "Monitor.h"
#include "led.h"
#include "audio.h"
#include "voice_client.h"


#define container_of(ptr,type,member)\
((type *)((char *)(ptr)-(unsigned long)&((type *)0)->member))

#define ORIGINAL_POS 2
#define MAX_DEVICE_NAME_LEN 16


//for key level
enum key_level{
    PIN_LOW,
    PIN_HIGH
};

typedef struct list_t{
    struct list_t *next;
    struct list_t *prev;
}mList_t;

typedef struct Header{
    mList_t head;
}Header_t;

typedef struct Node{
    char name[MAX_DEVICE_NAME_LEN];
    char label[MAX_DEVICE_NAME_LEN];
    int state;
    int *state_index;
    char **state_table;
    mList_t list;
    void (*func)(struct Node *node,int d);
}Node_t;


void motor_adjust(struct Node *node,int d);
void led_adjust(struct Node *node,int d);
void volume_adjust(struct Node *node,int d);
void voice_adjust(struct Node *node,int d);

void list_init(Header_t *header);
void list_insert_tail(Header_t *header,Node_t *node);
int list_isEmpty(Header_t *header);

#endif