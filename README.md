infinite_push_rtmp
==================

[![Build Status](https://travis-ci.org/sqbing/infinite_push_rtmp.svg?branch=master)](https://travis-ci.org/sqbing/infinite_push_rtmp)

基于[librtmp](https://rtmpdump.mplayerhq.hu/)实现的流推送程序，循环读取输入的flv文件，并推送至远端的RTMP服务器。

RTMP push program based on [librtmp](https://rtmpdump.mplayerhq.hu/).
Reads in one flv file, and pushes to remote RTMP server infinitely by adapting timestamp.

编译/Compile
==================
g++ -i main.cc -l rtmp -o infinite_push

运行/Run
==================
./infinite_push -i ~/whatever.flv -o rtmp://xxx.xxx.xxx.xxx/live/test

测试/Test
==================
已测试，运行良好:
* crtmpserver-1.1_beta
* nginx-rtmp-module-1.1.4
