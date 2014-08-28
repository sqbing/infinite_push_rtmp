/**
 * @file main.cc
 * @brief 
 * @author Rider Woo
 * @version 0.0.1
 * @date 2014-08-28
 */
#include <iostream>

extern "C"{
#include <librtmp/rtmp.h>
#include <librtmp/log.h>
}
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>

#define uchar unsigned char
struct GlobalContext{
    RTMP *rtmp;
    RTMPPacket *packet;
    FILE *fp;
    char *url;
    char *flvfile;
    uint32_t timestamp_delta;

    GlobalContext(){
        rtmp = NULL;
        packet = NULL;
        url = NULL;
        flvfile = NULL;
        fp = NULL;
        timestamp_delta = 0;
    }
    ~GlobalContext(){
        if(rtmp)
        {
            RTMP_Close(rtmp);
            RTMP_Free(rtmp);
            rtmp = NULL;
        }
        if(packet){
            RTMPPacket_Free(packet);
            delete packet;
            packet = NULL;
        }
        if(fp){
            fclose(fp);
            fp = NULL;
        }
    }

};

bool Read8(int &i8, FILE *fp){
    if(fread(&i8, 1, 1, fp) != 1){
        return false;
    }
    return true;
}
bool Read16(int &i16, FILE *fp){
    if(fread(&i16, 2, 1, fp) != 1){
        return false;
    }
    i16 = htons(i16);
    return true;
}
bool Read24(int &i24, FILE *fp){
    uchar byte;
    if(fread(&byte, 1, 1, fp) != 1){
        return false;
    }
    i24 += byte << 16;
    if(fread(&byte, 1, 1, fp) != 1){
        return false;
    }
    i24 += byte << 8;
    if(fread(&byte, 1, 1, fp) != 1){
        return false;
    }
    i24 += byte;
    return true;
}
bool Read32(int &i32, FILE *fp){
    if(fread(&i32, 4, 1, fp) != 1){
        return false;
    }
    i32 = htonl(i32);
    return true;
}
bool ReadTime(uint32_t &time, FILE *fp){
    if(fread(&time, 4, 1, fp) != 1){
        return false;
    }
    time = ((time>>16&0xff)|(time<<16&0xff0000)|(time&0xff00)|(time&0xff000000));
    return true;
}

void print_help(){
    std::cout<<"Commands:"<<std::endl;
    std::cout<<"    -h  print help information"<<std::endl;
    std::cout<<"    -i  input flv file"<<std::endl;
    std::cout<<"    -o  output rtmp url"<<std::endl;
}

int exit_program = 0;
void int_handler(int sig){
    exit_program = 1;
}

int connect_remote(GlobalContext &ctx){
    std::cout<<"Before allco rtmp context."<<std::endl;
    if(ctx.rtmp)
    {
        RTMP_Close(ctx.rtmp);
        RTMP_Free(ctx.rtmp);
        ctx.rtmp = NULL;
    }
    if(ctx.packet){
        RTMPPacket_Free(ctx.packet);
        delete ctx.packet;
        ctx.packet = NULL;
    }

    // 初始化librtmp参数
    ctx.rtmp = RTMP_Alloc();
    std::cout<<"Alloced librtmp struct."<<std::endl;
    if(!ctx.rtmp){
        std::cerr<<"Failed to alloc librtmp struct"<<std::endl;
        return -1;
    }
    RTMP_Init(ctx.rtmp);
    ctx.rtmp->Link.timeout = 5;
    std::cout<<"Timeout set to 5s."<<std::endl;

    // 创建RTMP数据包
    ctx.packet = new RTMPPacket;
    RTMPPacket_Alloc(ctx.packet, 1024*64);
    std::cout<<"Alloced rtmp packet."<<std::endl;
    RTMPPacket_Reset(ctx.packet);

    // 连接url
    RTMP_SetupURL(ctx.rtmp, ctx.url);
    std::cout<<"Setup rtmp url ["<<ctx.url<<"]."<<std::endl;
    RTMP_EnableWrite(ctx.rtmp);
    std::cout<<"Enabled rtmp write."<<std::endl;

    // NetConnect
    if(!RTMP_Connect(ctx.rtmp, NULL)){
        std::cout<<"Failed to do rtmp connect."<<std::endl;
        return -1;
    }
    std::cout<<"RTMP connected."<<std::endl;

    // StreamConnect
    if(!RTMP_ConnectStream(ctx.rtmp, 0)){
        return -1;
    }
    std::cout<<"Stream connected."<<std::endl;

    // 初始化RTMP数据包
    ctx.packet->m_hasAbsTimestamp = 0;
    ctx.packet->m_nChannel = 0x04;
    ctx.packet->m_nInfoField2 = ctx.rtmp->m_stream_id;
    return 0;
}

int open_file(GlobalContext &ctx){
    if(ctx.fp){
        fclose(ctx.fp);
        ctx.fp = NULL;
    }
    // 打开flv文件
    ctx.fp = fopen(ctx.flvfile, "rb");
    if(!ctx.fp){
        std::cout<<"Failed to open flv file "<<ctx.flvfile<<std::endl;
        return -1;
    }
    std::cout<<"Succeeded to open flv file "<<ctx.flvfile<<std::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    GlobalContext ctx;

    // 解析输入参数
    int opt;
    memset(&ctx, 0, sizeof(ctx));
    while((opt = getopt(argc, argv, "hi:o:")) != -1){
        switch(opt){
            case 'i':
                ctx.flvfile = optarg;
                break;
            case 'o':
                ctx.url = optarg;
                break;
            case 'h':
                print_help();
                return 0;
            default:
                break;
        }
    }
    if(!ctx.flvfile){
        std::cout<<"Input flv file not found."<<std::endl;
        print_help();
        return -1;
    }
    if(!ctx.url){
        std::cout<<"Output RTMP url not found."<<std::endl;
        print_help();
        return -1;
    }

    // 捕捉系统信号
    signal(SIGINT, int_handler);
    signal(SIGTERM, int_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // 设置日志级别
    RTMP_LogSetLevel(RTMP_LOGINFO);
    std::cout<<"Setting librtmp log level."<<std::endl;

    // 打开文件
    if(open_file(ctx)){
        return -1;
    }

    // 起始时间
    time_t start = time(0);
    // 上一帧时间戳
    uint32_t stream_timestamp = 0;

    while(!exit_program){
        // 连接rtmp服务端
        if(!ctx.rtmp || (ctx.rtmp && !RTMP_IsConnected(ctx.rtmp))){
            if(connect_remote(ctx)){
                // 每隔1s，重连rtmp服务端
                std::cout<<"Failed to connect to remote rtmp server, retry in 1 second...."<<std::endl;
                sleep(1);
                continue;
            }
        }

        // 跳过9字节长度
        fseek(ctx.fp, 9, SEEK_SET);
        // 跳过4字节长度
        fseek(ctx.fp, 4, SEEK_CUR);

        // 发送文件
        while(!exit_program){
            time_t cur = time(0);
            // 如果发送过快，等待...
            if((uint32_t)(cur - start) < (uint32_t)(stream_timestamp/1000)){
                usleep(300);
                continue;
            }
            std::cout<<"====================================================="<<std::endl;
            std::cout<<"cur: "<<cur<<" start: "<<start<<" stream timestamp: "<<stream_timestamp<<std::endl;
            std::cout<<"cur-start = "<<cur-start<<" stream_timestamp/1000 = "<<stream_timestamp/1000<<std::endl;

            // 类型
            int type = 0;
            int datalength = 0;
            uint32_t time = 0;
            int streamid = 0;

            // 从flv文件中读取数据包
            std::cout<<"File position: "<<ftell(ctx.fp)<<std::endl;
            if(!Read8(type, ctx.fp)){
                std::cout<<"Failed to read type"<<std::endl;
                break;
            }
            std::cout<<"type: "<<type<<std::endl;
            if(!Read24(datalength, ctx.fp)){
                std::cout<<"Failed to read datalength"<<std::endl;
                break;
            }
            std::cout<<"datalength: "<<datalength<<std::endl;
            if(!ReadTime(time, ctx.fp)){
                std::cout<<"Failed to read time"<<std::endl;
                break;
            }
            std::cout<<"time: "<<time<<std::endl;
            time += ctx.timestamp_delta;
            if(!Read24(streamid, ctx.fp)){
                std::cout<<"Failed to read streamid"<<std::endl;
                break;
            }
            std::cout<<"streamid: "<<streamid<<std::endl;
            if(type != 0x08 && type != 0x09){
                fseek(ctx.fp, datalength + 4, SEEK_CUR);
                continue;
            }

            if(fread(ctx.packet->m_body, 1, datalength, ctx.fp) != datalength){
                std::cout<<"Failed to read body"<<std::endl;
                break;
            }

            ctx.packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
            ctx.packet->m_nTimeStamp = time;
            ctx.packet->m_packetType = type;
            ctx.packet->m_nBodySize = datalength;

            if(!RTMP_IsConnected(ctx.rtmp)){
                std::cout<<"RTMP disconnected."<<std::endl;
                break;
            }

            // 发送RTMP数据包
            if(!RTMP_SendPacket(ctx.rtmp, ctx.packet, 0)){
                std::cout<<"RTMP packet sent."<<std::endl;
                break;
            }

            int alldatalength = 0;
            if(!Read32(alldatalength, ctx.fp)){
                std::cout<<"Failed to read all data length"<<std::endl;
                break;
            }
            stream_timestamp = time;
        }
        // 文件发送结束后，保存已发送timestamp
        ctx.timestamp_delta = stream_timestamp;
    }
    std::cout<<"exit"<<std::endl;
    return 0;
}
