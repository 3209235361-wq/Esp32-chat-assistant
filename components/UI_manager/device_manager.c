#include "device_manager.h"
#include "Monitor.h"
#include "led.h"
#include "audio.h"
#include <assert.h>

void list_init(Header_t *header){
    assert(header);
    header->head.next=&header->head;
    header->head.prev=&header->head;
}
void list_insert_tail(Header_t *header,Node_t *node){
    assert(header);
    assert(node);
    header->head.prev->next=&node->list;
    node->list.prev=header->head.prev;
    header->head.prev=&node->list;
    node->list.next=&header->head;
}

int list_isEmpty(Header_t *header){
    assert(header);
    return (header->head.next == &header->head)?-1:0;
}

void motor_adjust(struct Node *node,int d){
    assert(node);
    int state=node->state;
    state+=d;
    if(state < MOTOR_CLOSE_T) state=MAX_SPEED_T;
    if(state > MAX_SPEED_T) state=MOTOR_CLOSE_T;
    node->state=state;
    motor_set_speed(node->state_index[state]);
}

void led_adjust(struct Node *node,int d){
    assert(node);
    int state=node->state;
    state+=d;
    if(state < LED_CLOSE_T) state=LED_MAX_T;
    if(state > LED_MAX_T) state=LED_CLOSE_T;
    node->state=state;
    Set_Level_LED(node->state_index[state]);
}

void volume_adjust(struct Node *node,int d){
    assert(node);
    int state=node->state;
    state+=d;
    if(state < VOLUME_CLOSE_T) state=VOLUME_MAX_T;
    if(state > VOLUME_MAX_T) state=VOLUME_CLOSE_T;
    node->state=state;
    spk_set_volume(node->state_index[state]);
}
void voice_adjust(struct Node *node,int d){
    assert(node);
    int state=node->state;
    state+=d;
    if(state < STD_CHINESE_FM_MODEL) state=ENGLISH_M_MODEL;
    if(state > ENGLISH_M_MODEL) state=STD_CHINESE_FM_MODEL;
    node->state=state;
    vocal_line_set(state);
}