### 为录音任务创建：双缓冲区+双信号量 ###
int16_t *rec_buf[2]={NULL,NULL};
int rec_buf_index=0;
struct msg{
    int rec_buf_index;
    int samples;
};
struct msg msg={0,0};
Task Mic:
    1. 麦克风录音
       唤醒词 + 用户数据
       识别到唤醒词后，Take信号量（buf_free[rec_buf_index]）
    2. 将samples和rec_buf_index赋值给msg->
       rec_buf_index=1-rec_buf_index->                   
       msg发送到队列（audio_queue）
    

Task Play:
    1. 从队列（audio_queue）接收msg
    2. 播放录音数据
    3. Give信号量（buf_free[msg.rec_buf_index]）

### 目的 ###
双缓冲：替换调之前的mutex，因为Task Play持锁时间过长（最长30s），
导致Mic任务无法及时接收下一次的录音请求。
双信号量：同步Mic任务和Play任务，防止连续三次录音写坏前两次的录音数据。