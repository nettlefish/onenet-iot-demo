/**
 * for PI Zero :
 * gcc -g -o device_run device_run.c minIni.o -lzlog  -lpaho-mqtt3as -lcrypto -lcjson -lwiringPi -lpthread
 *
 **/

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <malloc.h>
#include <sys/sysinfo.h>
#include <endian.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include "MQTTAsync.h"
#include <unistd.h>
#include "zlog.h"
#include "minIni.h"
#include <cjson/cJSON.h>

#define PI_MODE	1
#define TIMEOUT     10000L
#define MAXHSTN 8
#define ZCCONFIG            "default1.conf"
#define TEMP_FILE_PATH "/sys/class/thermal/thermal_zone0/temp"
#define MAX_SIZE 32
#define NETWORK_FILE "/etc/network/interfaces"
#define MAX17040_ADDRESS        0x36
#define VCELL_REGISTER          0x02
#define SOC_REGISTER            0x04
#define MODE_REGISTER           0x06
#define VERSION_REGISTER        0x08
#define CONFIG_REGISTER         0x0C
#define COMMAND_REGISTER        0xFE
#define PowerDown_th    30
#define STATFILENAME "mqtt_device_run_stat.txt"

#define sizearray(a)  (sizeof(a) / sizeof((a)[0]))

#ifdef PI_MODE
#include <wiringPi.h>
#include <wiringPiI2C.h>
#endif


#ifdef GCC_GUN_C
#define HEX_TO_DEC(ch) \
({ \
    char ret = -1; \
    if (ch >= '0' && ch <= '9') ret = ch - '0'; \
    else if (ch >= 'a' && ch <= 'z') ret = ch - 'a' + 10; \
    else if (ch >= 'A' && ch <= 'Z') ret = ch - 'A' + 10; \
    else ret = -1; \
    ret;\
})
#else
int hex2dec(const char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    return -1;
}
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
// No translation needed for big endian system
#define Swap2Bytes(val) val
#define Swap4Bytes(val) val
#define Swap8Bytes(val) val
#else

// Swap 2 byte, 16 bit values:

#define Swap2Bytes(val) \
 ( (((val) >> 8) & 0x00FF) | (((val) << 8) & 0xFF00) )

// Swap 4 byte, 32 bit values:

#define Swap4Bytes(val) \
 ( (((val) >> 24) & 0x000000FF) | (((val) >>  8) & 0x0000FF00) | \
   (((val) <<  8) & 0x00FF0000) | (((val) << 24) & 0xFF000000) )

// Swap 8 byte, 64 bit values:

#define Swap8Bytes(val) \
 ( (((val) >> 56) & 0x00000000000000FF) | (((val) >> 40) & 0x000000000000FF00) | \
   (((val) >> 24) & 0x0000000000FF0000) | (((val) >>  8) & 0x00000000FF000000) | \
   (((val) <<  8) & 0x000000FF00000000) | (((val) << 24) & 0x0000FF0000000000) | \
   (((val) << 40) & 0x00FF000000000000) | (((val) << 56) & 0xFF00000000000000) )
#endif


#ifndef _COLOR_H_
#define _COLOR_H_
#define NONE	"\e[0m"  			  /* resume	*/       
#define BLACK	"\e[0;30m"      			
#define L_BLACK	"\e[1;30m"      
#define RED	"\e[0;31m"
#define L_RED	"\e[1;31m"
#define GREEN	"\e[0;32m"
#define L_GREEN	"\e[1;32m"
#define BROWN	"\e[0;33m"
#define YELLOW	"\e[1;33m"
#define BLUE	"\e[0;34m"
#define L_BLUE	"\e[1;34m"
#define PURPLE	"\e[0;35m"
#define L_PURPLE	"\e[1;35m"
#define CYAN	"\e[0;36m"
#define L_CYAN	"\e[1;36m"
#define GRAY	"\e[0;37m"
#define WHITE	"\e[1;37m"	/* white	*/
#define BOLD	"\e[1m"		/* white	*/		
#define UNDERLINE	"\e[4m"
#define BLINK	"\e[5m"
#define REVERSE	"\e[7m"
#define HIDE	"\e[8m"
#define CLEAR	"\e[2J"
#define CLRLINE	"\r\e[K" 
#endif


volatile MQTTAsync_token deliveredtoken;
/* signals		*/
volatile int finished = 0;
volatile int exitmainloop = 0;
volatile int disc_finished = 0;
volatile int subscribed = 0;
volatile int published = 0;
volatile int connected = 0;
/* count for stat	*/
volatile long stat_con = 0;
volatile long stat_conlos = 0;
volatile long stat_confail = 0;
volatile long stat_sub = 0;
volatile long stat_subfail = 0;
volatile long stat_pub = 0;
volatile long stat_pubfail = 0;
volatile long stat_pubacp = 0;
volatile long stat_pubdeny = 0;
volatile long stat_disconn = 0;
volatile long stat_disconnfail = 0;
volatile long stat_mainloop = 0;
volatile long stat_cmd = 0;
volatile long stat_invalidcmd =  0; 
volatile long stat_msgrcvd = 0;
volatile long stat_msgsend = 0;
volatile long stat_collect = 0;

const char Config_File[] = "mib_mqtt_v1.ini";
static zlog_category_t *zc;



typedef struct pubsub_opts_struct
{
        int publisher;  
        int quiet;
        int verbose;
        int tracelevel;
        char* delimiter;
        int maxdatalen;
	/* second line	*/
        char* message;
        char* filename;
        int stdin_lines;
        int stdlin_complete;
        char* payload_message;
	/*third line	*/
        int MQTTVersion;
        char* pub_topic;
        char* clientid;
        int qos;
        int retained;
        char* username;
        char* password;
        char* host;
        char* sub_topic;
        char* connection;
        int keepalive;
	/*Forth line	*/
        int insecure;
        char* cafile;
	char *base_sub_topic_fmt;
	char *base_json_dp_upload_topic_fmt;
	char *base_cmd_respon_topic_fmt;
	/*Fifth line	*/
	char *alarm_level;
	int critical_deta_time;
	int normal_deta_time; 
	long token_deta_time;
	int dynamic_token_ready;
	char* device_id;
	/*seventh line	*/
	char* device_key;
	char* device_dm;
	long last_token_time;
	char *token_version;
	int cleansession;
	int stop_publish;
	/*eighth line	*/
	int current_interval;
	char* start_time_str;
	
}pubsub_opts_struct;

struct pubsub_opts_struct common_opts =
{
	0,0,0,0,"\n", 100,
	NULL,NULL,1,0,NULL,
	MQTTVERSION_3_1_1,NULL,NULL,0,0,NULL,NULL,"ssl://183.230.40.16:8883",NULL,NULL,60,
	0,"./MQTTS-certificate.pem","$sys/%s/%s/#","$sys/%s/%s/dp/post/json","$sys/%s/%s/cmd/response/%s",
	"normal",60,600,0,0,NULL,
	NULL,NULL,0,"2018-10-31",1,0,
	600,NULL,
};


void connlost(void *context, char *cause);
int msgarrvd(void *context, char *topicName, int topicLen, MQTTAsync_message *message);
void handle_dp_response(const char *topic_name,void *payload, int payload_len);
void handle_cmd(void* context, const char *topic_name,void *payload, int payload_len);
void handle_cmd_response(void* context,const char *topic_name,void *payload, int payload_len);
void onDisconnect(void* context, MQTTAsync_successData* response);
void onSubscribe(void* context, MQTTAsync_successData* response);
void onSubscribeFailure(void* context, MQTTAsync_failureData* response);
void onConnectFailure(void* context, MQTTAsync_failureData* response);
void onConnect(void* context, MQTTAsync_successData* response);
void onDisconnectFailure(void* context, MQTTAsync_failureData* response);
void on_message_delivered(void *context, MQTTAsync_token dt);
void trace_callback(enum MQTTASYNC_TRACE_LEVELS level, char* message);
void cfinish(int sig);
const char* hostidstr();
void longitostr(long int x,char *p);
int loadDataintoStru(struct pubsub_opts_struct* opts);
int creatToken(struct pubsub_opts_struct* opts);
void handle_cmd_request(void* context, const char *topic_name,void *payload, int payload_len);
void hsleep(int s);
int Base64Decode(char* b64message, unsigned char** buffer, size_t* length);
int Base64Encode(const unsigned char* buffer, size_t length, char** b64text);
int urlencode(char* *lpszDest, const char* cszSrc, size_t nLen);
int urldecode(char* *lpszDest, const char* pszUrl, size_t nLen);
static int onSSLError(const char *str, size_t len, void *context);
void onPublishFailure(void* context, MQTTAsync_failureData* response);
void onPublish(void* context, MQTTAsync_successData* response);
void initconnect(void* context);
float getVCell();
float getSoC();
void quickStart();
void power_down_sys();
float Get_Cpu_Temperature(void);
char* getPowerState(char* PowerSatateUB);
char* getwlanip(char* wip_buf);
void Collect_CreatjSON(struct pubsub_opts_struct* opts);
int commonpublish(void* context,struct pubsub_opts_struct* opts);
char* executeRemoteCmd(char* cmdString, struct pubsub_opts_struct* opts,void* context);
void destoryandExit(void *context);
void OutPutStatReport(char *startt, char *endt);


int  main(int argc, char* argv[])
{
	int rc;
	int ch;
	int inirc;
        char StartTimeStr[42] = {'\0'};
        char EndTimeStr[42] = {'\0'};
        time_t StartTime = 0;
        time_t EndTime = 0;
        struct tm *startTimeSt = NULL;
        struct tm *endTimeSt = NULL;


	/*init zlog	*/
        rc = zlog_init(ZCCONFIG);
        if (rc) {
                printf("zlog init failed\n");
                return -1;
        }

        zc = zlog_get_category("cat1");
        if (!zc) {
                printf("zlog get cat fail\n");
                zlog_fini();
                return -2;
        }

        zlog_info(zc, "Hello, device start mqtt clent programe.");
	printf(BOLD"Hello, device start mqtt clent programe.\n"NONE);

        StartTime = time(NULL);
        startTimeSt = localtime(&StartTime);
        sprintf(StartTimeStr, "%04d-%02d-%02d/%02d:%02d:%02d", \
                startTimeSt->tm_year + 1900, startTimeSt->tm_mon + 1, \
                startTimeSt->tm_mday, startTimeSt->tm_hour, \
                startTimeSt->tm_min, startTimeSt->tm_sec);

	common_opts.start_time_str = strdup(StartTimeStr);
	zlog_info(zc,"device stat start time: %s",common_opts.start_time_str);


	/*signal preparation*/

	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = cfinish;
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	while (!exitmainloop) {
	
	stat_mainloop = stat_mainloop +1;

	/*load data/parameters into structure area      */

        loadDataintoStru(&common_opts);

        zlog_info(zc,"load data to structure.");
        zlog_info(zc,"common.username %s ",common_opts.username);
        zlog_info(zc,"device_id %s ",common_opts.device_id);
        zlog_info(zc,"device_key %s ",common_opts.device_key);
        zlog_info(zc,"client_id %s ",common_opts.clientid);
        zlog_info(zc,"device_dm %s ",common_opts.device_dm);
        zlog_info(zc,"device token deta time %ld",common_opts.token_deta_time);
        zlog_info(zc,"token last time %ld\n",common_opts.last_token_time);
        zlog_info(zc,"password after load from config file: %s",common_opts.password);
        zlog_info(zc,"sub topic: %s",common_opts.sub_topic);
        zlog_info(zc,"pub topic: %s",common_opts.pub_topic);
        zlog_info(zc,"payload: %s",common_opts.payload_message);
        zlog_info(zc,"current_interval:%d",common_opts.current_interval);
        zlog_info(zc,"stop_publish:%d",common_opts.stop_publish);
        zlog_info(zc,"end data load.");

        /*creat token password  */

        creatToken(&common_opts);
        zlog_info(zc,"Token/password after re-generate/re-check: %s",common_opts.password);



        MQTTAsync client;
        MQTTAsync_token token;
	MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
	

	/*Debug level setting			*/
	/*  *  ::MQTTASYNC_TRACE_MAXIMUM	*/
	/*  *  ::MQTTASYNC_TRACE_MEDIUM		*/
	/*  *  ::MQTTASYNC_TRACE_MINIMUM	*/
	/*  *  ::MQTTASYNC_TRACE_PROTOCOL	*/
	/*  *  ::MQTTASYNC_TRACE_ERROR		*/
	/*  *  ::MQTTASYNC_TRACE_SEVERE		*/
	/*  *  ::MQTTASYNC_TRACE_FATAL		*/
	/*  *  ::0				*/	

	if (common_opts.tracelevel > 0)
	{
		MQTTAsync_setTraceCallback(trace_callback);
		MQTTAsync_setTraceLevel(common_opts.tracelevel);
	}

	/*creat handle of mqtt connection	*/
	MQTTAsync_create(&client, common_opts.host, common_opts.clientid, MQTTCLIENT_PERSISTENCE_NONE, NULL);

	/*set Call back points connlost 	*/
	MQTTAsync_setCallbacks(client, client, connlost, msgarrvd, on_message_delivered);	

	/* init the connect to broker	*/

	initconnect(client);

	/* waiting client sub to broker      	*/
    
	while	(!subscribed && !finished)
			hsleep(1);
	
	/*main loop in periodic publishment	*/
        commonpublish(client,&common_opts);


	/*exit preparing	*/

	disc_opts.onSuccess = onDisconnect;
	disc_opts.onFailure = onDisconnectFailure;

	if ((rc = MQTTAsync_disconnect(client, &disc_opts)) != MQTTASYNC_SUCCESS)
	{
		zlog_info(zc,"Failed to start disconnect, return code %d", rc);
		goto exit;
	}

 	while	(!disc_finished)
			hsleep(2);
	
	exit:
		MQTTAsync_destroy(&client);
		finished = 0;

	}

        EndTime = time(NULL);
        endTimeSt= localtime(&EndTime);
        sprintf(EndTimeStr, "%04d-%02d-%02d/%02d:%02d:%02d", \
                endTimeSt->tm_year + 1900, endTimeSt->tm_mon + 1, \
                endTimeSt->tm_mday, endTimeSt->tm_hour, \
                endTimeSt->tm_min, endTimeSt->tm_sec);
	
	OutPutStatReport(StartTimeStr,EndTimeStr);
	zlog_fini();
	return 1;

}


void connlost(void* context, char *cause)
{

	stat_conlos = stat_conlos + 1;

        printf("connlost:       Connection lost");
        if (cause)
                printf("    with cause: %s\n", cause);

        printf("connlost: Try to reconnect...\n");

	zlog_info(zc,"connlost: Try to reconnect,with cause: %s\n", cause);

	connected = 0;
	subscribed = 0;
	finished = 1;
	common_opts.stop_publish = 1;
}


int msgarrvd(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
	int i;
	char *payloadptr;
	char *dp_pos = NULL;
	char *cmd_pos = NULL;

	stat_msgrcvd = stat_msgrcvd + 1;

	if (message->payloadlen > 0) {
		printf( L_GREEN "Message arrived,topic: [%s], payload length: [%d],message: ",topicName,message->payloadlen);

		payloadptr = message->payload;
		for(i=0; i<message->payloadlen; i++)
		{
			putchar(*payloadptr++);
		}
		printf(NONE"\n");
	
		zlog_info(zc,"megarrvd: Message arrived,topic: [%s], payload length: [%d], message: [%s]",topicName,message->payloadlen,message->payload);
		} else {
		printf(L_GREEN"Message arrived,topic: [%s], payload length: [%d]\n"NONE,topicName,message->payloadlen);
		zlog_info(zc,"megarrvd: Message arrived,topic: [%s], payload length: [%d]",topicName,message->payloadlen);	
	}

	MQTTAsync tmp_c = (MQTTAsync)context;

	dp_pos = strstr(topicName,"/dp/post/json/");
	if(NULL != dp_pos) {
		handle_dp_response(topicName,message->payload,message->payloadlen);
		zlog_info(zc,"megarrvd: recv dp response message from broker");
	}

	cmd_pos = strstr(topicName,"/cmd/");
	if(NULL != cmd_pos) {
		handle_cmd(tmp_c,topicName,message->payload,message->payloadlen);
		zlog_info(zc,"megarrvd: recv cmd from administor through broker");
	}

	MQTTAsync_freeMessage(&message);
	MQTTAsync_free(topicName);

	payloadptr=NULL;
	dp_pos=NULL;
	cmd_pos=NULL;	

	return 1;
}


void handle_dp_response(const char *topic_name,
                               void *payload, int payload_len) 
{
    char *accepted_pos = NULL;
    char *rejected_pos = NULL;

    accepted_pos = strstr(topic_name,"/accepted");
    if(NULL != accepted_pos) {
        zlog_info(zc,"handle_dp_response: recv dp accepted response message from broker.");
	stat_pubacp = stat_pubacp + 1;
        return;
    }

    rejected_pos = strstr(topic_name,"/rejected");
    if(NULL != rejected_pos) {
        char *err_msg = (char *)malloc(payload_len * sizeof(char) + 1);
        if(NULL == err_msg) {
            return;
        }
        memset(err_msg,'\0',payload_len + 1);
        strncpy(err_msg,(const char*)payload,payload_len);
        zlog_info(zc,"handle_dp_response: recv dp rejected response message from broker,with error message [%s]",err_msg);
        free(err_msg);
	stat_pubdeny = stat_pubdeny + 1;
        return;
    }
    zlog_info(zc,"handle_dp_response: rcv unknown dp response topic[%s]\n",topic_name);
}


void handle_cmd(void* context, const char *topic_name,
                       void *payload, int payload_len) 
{

	char *cmd_pos = NULL;
	cmd_pos = strstr(topic_name,"/cmd/request/");

        MQTTAsync tmp_c = (MQTTAsync)context;


	if(NULL != cmd_pos) {
		handle_cmd_request(tmp_c,topic_name,payload,payload_len);
	} else {
		handle_cmd_response(tmp_c,topic_name,payload,payload_len);
	}
}



void handle_cmd_request(void* context, const char *topic_name,
                               void *payload, int payload_len) 
{
	/*fmt:	$sys/{pid}/{authInfo}/cmd/response/{cmd_uudi}		*/
	/*ex:  [$sys/username/clientid/cmd/request/3dde715b-1255-1c20-4ace-eb00399035de]	*/
	
	const char *part_of_cmd_request_topic = "/cmd/request/";
	int part_of_cmd_request_topic_len = strlen(part_of_cmd_request_topic);

	/*ex:  /cmd/request/3dde715b-1255-1c20-4ace-eb00399035de		*/
	char *cmd_pos = strstr(topic_name,part_of_cmd_request_topic);

        MQTTAsync tmp_c = (MQTTAsync)context;
	
	zlog_info(zc,"handle_cmd_request: topic name:%s,cmd pos:%s",topic_name,cmd_pos);

	if(NULL != cmd_pos) {
	MQTTAsync temp_client = (MQTTAsync) context;
        MQTTAsync_message cmd_pubmsg = MQTTAsync_message_initializer;

        char cmd_response_topic[512] = {'\0'};

        char *cmd_response_content = "Get cmd and to be process...";

        int rc = 0;
        int i = 0;

        char *tmp = payload;
	
	/*ex: 3dde715b-1255-1c20-4ace-eb00399035de		*/
        printf(L_BLUE"Receive rmt administor cmd from broker: %s",tmp);
	
	zlog_info(zc,"handle_cmd_request: rcvd cmd from administor through broker: %s, and prepare to execute this cmd",tmp);
	printf("The command recvd from administor is:"L_RED" [%s]"NONE". And begin try to execute it...\n",tmp);
	
	/*execute cmd and get result string	*/
	cmd_response_content = strdup(executeRemoteCmd(tmp, &common_opts ,context));
	
	printf(L_CYAN"Executed rmt cmd, get response string:%s\n"NONE,cmd_response_content);	
	zlog_info(zc,"handle_cmd_request: executed rmt cmd, get response string:%s\n",cmd_response_content);

	zlog_info(zc,"handle_cmd_request: try to report broker the cmd result. rcvd cmd: %s, cmd_res_con:%s, respon_cmd_fmt: %s", tmp ,cmd_response_content,common_opts.base_cmd_respon_topic_fmt);	

        snprintf(cmd_response_topic,sizeof(cmd_response_topic), common_opts.base_cmd_respon_topic_fmt,
                 common_opts.username,
                 common_opts.clientid,
                 cmd_pos + part_of_cmd_request_topic_len);
	
	zlog_info(zc,"handle_cmd_request: before pub: %s and  %s\n",cmd_response_topic, cmd_response_content);

        cmd_pubmsg.payload = cmd_response_content;
        cmd_pubmsg.payloadlen = strlen(cmd_response_content);
        cmd_pubmsg.qos = 0;
        cmd_pubmsg.retained = 0;
	
	zlog_info(zc,"handle_cmd_request: before pub: %s  %s \n",cmd_response_topic,cmd_pubmsg.payload);

        if ((rc = MQTTAsync_send(temp_client, cmd_response_topic, cmd_pubmsg.payloadlen,cmd_pubmsg.payload,cmd_pubmsg.qos,cmd_pubmsg.retained,NULL )) != MQTTASYNC_SUCCESS) {
            zlog_info(zc,"handle_cmd_request: failed to start send message, return code %d\n", rc);
        }
	
    }

}

void handle_cmd_response(void* context,const char *topic_name,
                                void *payload, int payload_len) 
{

	/*$sys/username/clientid/cmd/response/3d10b128-1abe-4ab3-9641-1381a35ab02a		*/
	/*$sys/username/clientid/cmd/response/3d10b128-1abe-4ab3-9641-1381a35ab02a/accepted	*/

	char *response_accepted_pos = NULL;
	char *response_rejected_pos = NULL;
	char *tmp = NULL;;
	const char *part_of_cmd_response_topic = "/cmd/response/";
	int part_of_cmd_response_topic_len = strlen(part_of_cmd_response_topic);


	tmp = strstr(topic_name,part_of_cmd_response_topic);
	if(NULL != tmp) {
		response_accepted_pos = strstr(tmp,"/accepted");
	if(NULL != response_accepted_pos) {

		char *cmd_uuid = (char *)malloc(response_accepted_pos - tmp - part_of_cmd_response_topic_len + 1);
		if(NULL == cmd_uuid) {
			return;
			}
		memset(cmd_uuid,'\0',response_accepted_pos - tmp - part_of_cmd_response_topic_len + 1);
		strncpy(cmd_uuid,tmp + part_of_cmd_response_topic_len,response_accepted_pos - tmp - part_of_cmd_response_topic_len);

		zlog_info(zc,"handle_cmd_response:Broker accept the cmd[%s] response.",cmd_uuid);
		free(cmd_uuid);
		return;
	}

	response_rejected_pos = strstr(tmp,"/rejected");
	if(NULL != response_rejected_pos) {
		char *cmd_uuid = (char *)malloc(response_rejected_pos - tmp - part_of_cmd_response_topic_len + 1);
			if(NULL == cmd_uuid) {
			return;
			}

		memset(cmd_uuid,'\0',response_rejected_pos - tmp - part_of_cmd_response_topic_len + 1);
		strncpy(cmd_uuid,tmp + part_of_cmd_response_topic_len,response_rejected_pos - tmp - part_of_cmd_response_topic_len);
		zlog_info(zc,"handle_cmd_response: Broker reject the cmd[%s] response...",cmd_uuid);

		free(cmd_uuid);
		return;
		}
	 return;
    	}
    zlog_info(zc,"handle_cmd_response: unknown cmd response topic");
}


void onDisconnectFailure(void* context, MQTTAsync_failureData* response)
{
	stat_disconnfail = stat_disconnfail + 1;

	zlog_info(zc,"onDisconnectFailure: Disconnect failed, rc %d", response->code);
	disc_finished = 1;
	
}

void onDisconnect(void* context, MQTTAsync_successData* response)

{
	stat_disconn = stat_disconn + 1;	

	zlog_info(zc,"onDisconnect: Successful disconnection");
	printf("Successful disconnection.\n");
	disc_finished = 1;
}


void onSubscribe(void* context, MQTTAsync_successData* response)
{

	stat_sub = stat_sub + 1;

	zlog_info(zc,"onSubscribe: Subscribe succeeded");
	printf("Subscribe succeeded.\n");
	subscribed = 1;
}

void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
	stat_subfail = stat_subfail + 1;

	printf("Subscribe failed, rc %d\n", response ? response->code : 0);
	zlog_info(zc,"onSubscribeFailure: Subscribe failed, rc %d", response ? response->code : 0);
	subscribed = 0;
	
}


void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
	stat_confail = stat_confail + 1;

	printf("Connect failed, rc %d\n", response ? response->code : 0);
	zlog_info(zc,"onConnectFailure: Connect failed, rc %d\n", response ? response->code : 0);
	connected = 0;
}

void on_message_delivered(void *context, MQTTAsync_token dt) 
{
	
	stat_msgsend = stat_msgsend + 1;

  	zlog_info(zc,"on_message_delivered:message with token value %d delivery confirmed", dt); 
	deliveredtoken = dt;
}

void onPublish(void* context, MQTTAsync_successData* response)
{
	
	stat_pub = stat_pub + 1;
	
	zlog_info(zc,"onPublish:Message sent. token value %d", response->token);
        zlog_info(zc,"onPublish:Publish succeeded");
        published = 1;
}

void onPublishFailure(void* context, MQTTAsync_failureData* response)
{ 
	
	stat_pubfail = stat_pubfail + 1;
	
        zlog_info(zc,"onPublishFailure:Publish failed, rc %s", MQTTAsync_strerror(response->code));
        published = 0;
}



void onConnect(void* context, MQTTAsync_successData* response)
{
	stat_con = stat_con + 1;

	MQTTAsync client = (MQTTAsync)context;
	MQTTAsync_responseOptions sub_opts = MQTTAsync_responseOptions_initializer;	

	int rc;


	printf("Successful connection!\n");
	zlog_info(zc,"onConnect: Successful connection");
	printf("Subscribing to topic %s for client %s using QoS%d\n\n"
           "Press Ctrl+c  to quit\n\n", common_opts.sub_topic, common_opts.clientid, common_opts.qos);

	sub_opts.onSuccess = onSubscribe;
	sub_opts.onFailure = onSubscribeFailure;
	sub_opts.context = client;

	deliveredtoken = 0;

	if ((rc = MQTTAsync_subscribe(client, common_opts.sub_topic, common_opts.qos, &sub_opts)) != MQTTASYNC_SUCCESS)
	{
		zlog_info(zc,"onConnect: Failed to start subscribe, return code %d", rc);
		printf("Failed to start subscribe, return code %d\n", rc);
		subscribed = 0;
		finished = 1;
	}
	connected = 1;
	subscribed = 1;
	common_opts.stop_publish = 0;

}



void trace_callback(enum MQTTASYNC_TRACE_LEVELS level, char* message)
{
        zlog_info(zc, "trace_callback: Trace : %d, %s", level, message);
}

void cfinish(int sig)
{
        signal(SIGINT, NULL);
        finished = 1;	
	exitmainloop = 1;
	printf("Rcv ctrl+c, the exit flag has been set!\n");
	zlog_info(zc,"cfinish: Rcv ctrl+c, the exit flag has been set.");

}

const char* hostidstr()
{
        long    result;
        int ii;
        char s[MAXHSTN] = {'\0'}; 
        static char t[MAXHSTN] =  {'\0'};


        if(-1==(result=gethostid()))
        {
                zlog_info(zc,"hostidstr: gethostid err");
                exit(0);
        }
        zlog_info(zc,"hostidstr:hosid is :%x",result);

        longitostr(result,s);

     /*   printf("%s \n",s);	*/

        if (strlen(s) < 8) {
                for (ii=0; ii<(8-strlen(s)); ii++)
                {
                        strcat(t,"0");

                }

                strcat(t,s);
        }

        zlog_info(zc,"hostidstr: %s",t);
        return t;
}

void longitostr(long int x,char *p)
{
        int div;
        int k=0;
        if(x==0){
                *p='0';
        }
        if(x<0){
                x=-x;
                *p='-';
                p++;
        }
        for(int i=0;x!=0;i++){
                div=x%16;
                x=x/16;
                if((div-10)<0)
                *(p+i)=div+'0';
                else *(p+i)='a'+div-10;
                div=0;
                k++;
        }
        //reverse
        for(int i=0;i<k/2;i++){
                char temp=p[i];
                p[i]=p[k-1-i];
                p[k-1-i]=temp;
        }
}


int  loadDataintoStru(struct pubsub_opts_struct* opts)
{

        long n;
	int irc;
        char device_name_l[40] = {'\0'};
        char temp_str[180] = {'\0'};
	char pubsub_topic[512] = {'\0'};


        zlog_info(zc, "Load Data from config file to structure.");

        n = ini_gets("List",hostidstr(), "", device_name_l, sizearray(device_name_l), Config_File);

        zlog_info(zc,"The length of device_name_l readin is: [%d]",n);
        zlog_info(zc,"The device_name_l readin is: [%s]", device_name_l);

	n = ini_gets(device_name_l, "Product_ID", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of login username readin is: [%d]",n);
        zlog_info(zc,"The login username readin is: [%s]", temp_str);
        opts->username = strdup(temp_str);	
	zlog_info(zc,"The login username set to: [%s]", opts->username);

        n = ini_gets(device_name_l, "Device_ID", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of device readin is: [%d]",n);
        zlog_info(zc,"The device_id readin is: [%s]", temp_str);
        opts->device_id = strdup(temp_str);
        zlog_info(zc,"The device_id set to: [%s]", opts->device_id);

	n = ini_gets(device_name_l, "Device_key", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of device readin is: [%d]",n);
        zlog_info(zc,"The device_key readin is: [%s]", temp_str);
        opts->device_key = strdup(temp_str);
        zlog_info(zc,"The device_key set to: [%s]", opts->device_key);

        n = ini_gets(device_name_l, "Device_name", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of device readin is: [%d]",n);
        zlog_info(zc,"The device_name readin is: [%s]", temp_str);
        opts->clientid = strdup(temp_str);
        zlog_info(zc,"The device_key set to: [%s]", opts->clientid);

        n = ini_gets(device_name_l, "Digest_Method", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of device readin is: [%d]",n);
        zlog_info(zc,"The Digest_Method readin is: [%s]", temp_str);
        opts->device_dm =strdup(temp_str);
        zlog_info(zc,"The device_key set to: [%s]", opts->device_dm);

	opts->token_deta_time = ini_getl(device_name_l, "Token_Deta_Update",36000 ,Config_File);
	zlog_info(zc,"The value of token deta update : [%ld]",opts->token_deta_time);

        opts->last_token_time = ini_getl(device_name_l, "Last_Token_time",1595834604 ,Config_File);
        zlog_info(zc,"The value of last token creat time : [%ld]",opts->last_token_time);

        n = ini_gets(device_name_l, "Last_Token", "", temp_str, sizearray(temp_str), Config_File);
        zlog_info(zc,"The length of device passwd readin is: [%d]",n);
        zlog_info(zc,"The passwd readin is: [%s]", temp_str);
        opts->password =strdup(temp_str);
        zlog_info(zc,"The device_passwd set to: [%s]", opts->password);
	
	snprintf(pubsub_topic,sizeof(pubsub_topic),opts->base_sub_topic_fmt, opts->username, opts->clientid);	
	opts->sub_topic = strdup(pubsub_topic);

	snprintf(pubsub_topic,sizeof(pubsub_topic),opts->base_json_dp_upload_topic_fmt,opts->username, opts->clientid);
	opts->pub_topic = strdup(pubsub_topic);
	
	opts->current_interval = ini_getl(device_name_l, "Last_Interval_Setting",600 ,Config_File);
	zlog_info(zc,"The value of last interval setting : [%d]",opts->current_interval);
	
	if ( (opts->current_interval> 7200 ) || (opts->current_interval <60 )) {

		if (strcasecmp("Normal", opts->alarm_level) == 0)
                {
                        opts->current_interval = opts->normal_deta_time;

                } else if (strcasecmp("Critical", opts->alarm_level) == 0) {

                        opts->current_interval = opts->critical_deta_time;
                } else {
                        opts->current_interval = opts->normal_deta_time;
			}
		}
        opts->stop_publish = ini_getl(device_name_l, "Stop_Publish",1 ,Config_File);
        zlog_info(zc,"Publish flag setting restored: [%d]",opts->stop_publish);

	
	return 1;

}

void hsleep(int s)
{
	usleep((s *1000) *1000);
}

int creatToken(struct pubsub_opts_struct* opts)
{
	char token_s[220] = {'\0'};
        char res_input[100] = "products/";
        char *res_input_u = NULL;
        char *sign_b_u = NULL;
        char orgchars[220] = {'\0'};
        char temchars[220] = {'\0'};
        char timechar[25] = {'\0'};
        unsigned char digest[EVP_MAX_MD_SIZE] = {'\0'};
        unsigned int digest_len = 0;
        long  tmp_digit,n;
	int ii;
        long Time_V = 1595834604;
        time_t ti;


        tmp_digit = time(&ti);
	

        zlog_info(zc,"Get the current system time: [%ld]",tmp_digit);
	
        if ( tmp_digit < (opts->last_token_time + opts->token_deta_time - opts->critical_deta_time ))
        {
        zlog_info(zc,"Still in Token Validity period. [%ld] < [%ld] + [%d] -[%d]s",tmp_digit,opts->last_token_time,opts->token_deta_time,opts->critical_deta_time);
        zlog_info(zc,"The token return is: [%s]",opts->password);

        } else 
	 	{
		Time_V = tmp_digit + opts->token_deta_time;
		zlog_info(zc,"New token should be recreated, using time parameter is: [%d],with timing start value: [%d]",Time_V,tmp_digit);
		sprintf(timechar,"%d",Time_V);
		puts(timechar);
		strcpy(temchars, opts->device_key);
	        char* base64DecodeOutput;
		size_t test;
		Base64Decode(temchars, &base64DecodeOutput, &test);
		zlog_info(zc,"The access_key have base64 decoded is: [%s] ,with string length [%d]",base64DecodeOutput,test);
        	char* base64EncodeOutput;
		Base64Encode(base64DecodeOutput, strlen(base64DecodeOutput), &base64EncodeOutput);
		zlog_info(zc,"re-encode access_ley is: [%s]",base64EncodeOutput);
		strcat(orgchars,timechar);
		strcat(orgchars,"\n");
		strcat(orgchars,opts->device_dm);
		strcat(orgchars,"\n");
		strcat(orgchars,"products/");
		strcat(orgchars, opts->username);
		strcat(orgchars,"/devices/");
		strcat(orgchars, opts->clientid);
		strcat(orgchars,"\n");
		strcat(orgchars, opts->token_version);

		strcat(res_input,opts->username);
		strcat(res_input,"/devices/");
		strcat(res_input,opts->clientid);

		zlog_info(zc,"The combined string with time,digest,productid and system version is: [%s]",orgchars);
		zlog_info(zc,"The productid combined with prefix is: [%s]",res_input);
		urlencode(&res_input_u,res_input,strlen(res_input));
		zlog_info(zc,"Url encode the productid string is: [%s]", res_input_u);

		const EVP_MD * hmacengine = NULL;
		if(strcasecmp("sha512", opts->device_dm) == 0) {
			hmacengine = EVP_sha512();
			}
			else if(strcasecmp("sha256", opts->device_dm) == 0) {
			hmacengine = EVP_sha256();
			}
			else if(strcasecmp("sha1", opts->device_dm) == 0) {
			hmacengine = EVP_sha1();
			}
			else if(strcasecmp("md5", opts->device_dm) == 0) {
			hmacengine = EVP_md5();
			}
			else if(strcasecmp("sha224", opts->device_dm) == 0) {
			hmacengine = EVP_sha224();
			}
			else if(strcasecmp("sha384", opts->device_dm) == 0) {
			hmacengine = EVP_sha384();
			}
			else {
			zlog_info(zc,"Digest algorithm [%s]  is not supported by this program!", opts->device_dm) ;
			return -1;
			}

		HMAC(hmacengine, (unsigned char*)base64DecodeOutput, strlen(base64DecodeOutput),(unsigned char*)orgchars, strlen(orgchars), digest, &digest_len); 
		zlog_info(zc, "The digest created is: [%s] with length is: [%d]",digest, digest_len);
			printf("Digest string:");	
			for(ii = 0; ii < digest_len; ii++) {
				printf("%02X",(unsigned char)digest[ii]);
 					}	
			printf("\n");

		char* sign_b;
		Base64Encode(digest,digest_len,&sign_b);
		zlog_info(zc,"The hmac digest encode with base64: [%s]",sign_b);
		urlencode(&sign_b_u,sign_b,strlen(sign_b));
		zlog_info(zc, "The base64 encoded hmac digest convert to url-encoded: [%s]", sign_b_u);
		
		strcat(token_s,"version=");
		strcat(token_s,opts->token_version);
		strcat(token_s,"&res=");
		strcat(token_s,res_input_u);
		strcat(token_s,"&et=");
		strcat(token_s,timechar);
		strcat(token_s,"&method=");
		strcat(token_s,opts->device_dm);
		strcat(token_s,"&sign=");
		strcat(token_s,sign_b_u);
	
		zlog_info(zc, "The token can taken to auth is: [%s]", token_s);
		n = ini_putl(opts->clientid, "Last_Token_time", tmp_digit, Config_File);
		n = ini_puts(opts->clientid, "Last_Token", token_s,  Config_File);
			
		opts->password = strdup(token_s);
		opts->last_token_time = tmp_digit;
		opts->dynamic_token_ready = 1 ;

		if (res_input_u) free(res_input_u);
		if (sign_b_u) free(sign_b_u);
		
		}

	return 0;
}

size_t calcDecodeLength(const char* b64input) { //Calculates the length of a decoded string
        size_t len = strlen(b64input),
                padding = 0;

        if (b64input[len-1] == '=' && b64input[len-2] == '=') //last two chars are =
                padding = 2;
        else if (b64input[len-1] == '=') //last char is =
                padding = 1;

        return (len*3)/4 - padding;
}

int Base64Decode(char* b64message, unsigned char** buffer, size_t* length) { //Decodes a base64 encoded string
        BIO *bio, *b64;

        int decodeLen = calcDecodeLength(b64message);
        *buffer = (unsigned char*)malloc(decodeLen + 1);
        (*buffer)[decodeLen] = '\0';

        bio = BIO_new_mem_buf(b64message, -1);
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); //Do not use newlines to flush buffer
        *length = BIO_read(bio, *buffer, strlen(b64message));
        assert(*length == decodeLen); //length should equal decodeLen, else something went horribly wrong
        BIO_free_all(bio);

        return 0; 
}


int Base64Encode(const unsigned char* buffer, size_t length, char** b64text) { //Encodes a binary safe base 64 string
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); //Ignore newlines - write everything in one line
        BIO_write(bio, buffer, length);
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        BIO_set_close(bio, BIO_NOCLOSE);
        BIO_free_all(bio);

        *b64text=(*bufferPtr).data;

        return (0); 
}

int urlencode(char* *lpszDest, const char* cszSrc, size_t nLen)
{
    size_t i = 0, nIndex = 0;
    size_t len = nLen;
    char hex[] = "0123456789ABCDEF";
    char ch;
    char* pNew = NULL;

    if (!cszSrc)
        return -1;

    *lpszDest = (char *)malloc(nLen * sizeof(char) + 1); 
    if (NULL == *lpszDest)
        return -1;

    for (i = 0; i < nLen; i++) {
        ch = cszSrc[i];
        if ((ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            (*lpszDest)[nIndex++] = ch;
        }
/*        else if (ch == ' ') {
            (*lpszDest)[nIndex++] = '+';
        }  */
        else {
            int tmp = ch;
            if (ch < 0)
                tmp += 256;
            (*lpszDest)[nIndex++] = '%';
            (*lpszDest)[nIndex++] = hex[tmp / 16];
            (*lpszDest)[nIndex++] = hex[tmp % 16];
        }
        if (nIndex >= len - 1) {
            len *= 3;
            pNew = malloc(sizeof(char) * len);
            memset(pNew, '\0', sizeof(char) * len);
            if (!pNew)
                return -1;
            memcpy(pNew, *lpszDest, nIndex);
            free(*lpszDest);
            *lpszDest = pNew;
        }
    }
    *lpszDest = (char *)realloc(pNew, sizeof(char) * strlen(pNew) + 1); 
    if (!*lpszDest)
    {
        free(*lpszDest);
        return -1;
    }
    return 0;
}



int urldecode(char* *lpszDest, const char* pszUrl, size_t nLen)
{
    size_t i = 0, nIndex = 0;
    char ch;
    char* pNew = NULL;

    *lpszDest = (char *)malloc(nLen * sizeof(char));
    if (NULL == *lpszDest)
        return -1;
    memset(*lpszDest, '\0', nLen);

    for (i = 0; i < nLen; ++i) {
        ch = pszUrl[i];
        if (ch != '%') {
            (*lpszDest)[nIndex++] = ch;
        }
        else {
            char h = pszUrl[++i];
            char l = pszUrl[++i];
#ifdef GCC_GUN_C
            int num = (HEX_TO_DEC(h) << 4) | HEX_TO_DEC(l);
#else
            int num = (hex2dec(h) << 4) | hex2dec(l);
#endif
            (*lpszDest)[nIndex++] = num;
        }
    }
    pNew = (char*)malloc(sizeof(char) * strlen(*lpszDest) + 1);
    if (!pNew)
        return -1;
    strncpy(pNew, *lpszDest, strlen(*lpszDest) + 1); 
    free(*lpszDest);
    *lpszDest = pNew;

    return 0;
}

static int onSSLError(const char *str, size_t len, void *context)
{
        MQTTAsync client = (MQTTAsync)context;
        zlog_info(zc, "onSSLError: SSL error: %s", str);
	return 1;
}

void initconnect(void* context)
{

	MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_SSLOptions sslopts = MQTTAsync_SSLOptions_initializer;	

	printf(BLINK"Connecting...\n"NONE);

	conn_opts.keepAliveInterval = common_opts.keepalive;
	conn_opts.cleansession = common_opts.cleansession;
	conn_opts.username = common_opts.username;
	conn_opts.password = common_opts.password;
	conn_opts.MQTTVersion = common_opts.MQTTVersion;
	conn_opts.automaticReconnect = 1;		
	conn_opts.will = NULL;
	conn_opts.onSuccess = onConnect;
	conn_opts.onFailure = onConnectFailure;
	conn_opts.context = client;	

	sslopts.enableServerCertAuth = 1;
	sslopts.trustStore=common_opts.cafile;
	sslopts.ssl_error_cb = onSSLError;
	sslopts.ssl_error_context = client;
	conn_opts.ssl = &sslopts;	

        int rc = 0;

        hsleep(1);

        while(!finished) {

                if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
                	{
                        printf("retry connect to broker,with return code %d\n", rc);
                        hsleep(5);
                        continue;
	                }
               connected = 1;
               break;
                }

}


int commonpublish(void *context,struct pubsub_opts_struct* opts)
{

	zlog_info(zc,"Enter common publish section.");
	int rc;
        MQTTAsync client = (MQTTAsync)context;
        deliveredtoken = 0;
	MQTTAsync_responseOptions pub_opts = MQTTAsync_responseOptions_initializer;
	MQTTAsync_message pubmsg = MQTTAsync_message_initializer;

	int ch;
	long  check_time;
	time_t tc;
	check_time = time(&tc);
	
        Collect_CreatjSON(opts);

	pub_opts.onSuccess = onPublish;
	pub_opts.onFailure = onPublishFailure;
	pub_opts.context = client;
	pubmsg.payload = opts->payload_message;
	pubmsg.payloadlen = strlen(opts->payload_message);
	pubmsg.qos = opts->qos;
	pubmsg.retained = opts->retained;

	deliveredtoken = 0;

	zlog_info(zc,"commonpublish/while loop: current interval: %d",opts->current_interval);
	printf(REVERSE"Current publish/report interval setting: [%ds]\n"NONE,opts->current_interval);
	
	while (!finished)
	{		
	
               	creatToken(opts);
		printf(L_CYAN"Publish payload: %s, length: %d\n"NONE,opts->payload_message,strlen(opts->payload_message));	
		zlog_info(zc,"commonpublish/while loop: publish report message with interval:%d ",opts->current_interval);	
		zlog_info(zc,"pcommonpublish/while loop: ayload: %s, length: %d",opts->payload_message,strlen(opts->payload_message));
		zlog_info(zc,"commonpublish/while loop: alarm level: %s",opts->alarm_level);
		zlog_info(zc,"commonpublish/while loop: publish topic:%s",opts->pub_topic);
		zlog_info(zc,"commonpublish/while loop: current interval: %d",opts->current_interval);

		if ( opts->stop_publish == 0 ) {
		rc = MQTTAsync_sendMessage(client, opts->pub_topic, &pubmsg, &pub_opts);

        	if ( rc != MQTTASYNC_SUCCESS ) {
             		zlog_info(zc, "Error from MQTTAsync_send: %s", MQTTAsync_strerror(rc));
			finished = 1;	
			exit(EXIT_FAILURE);	
				}
			} else {
			printf("Stop report setting: %d, idle...\n",opts->stop_publish);
			zlog_info(zc,"commonpublish/while loop:Stop report setting: %d, idle...",opts->stop_publish);
			}	
		hsleep(opts->current_interval);
		Collect_CreatjSON(opts);
		

		printf(REVERSE"Current publish/report interval setting: [%ds]\n"NONE,opts->current_interval);
	}

	zlog_info(zc,"commonpublish:Exit from commonpub,with %d finished flag.",finished);

        return 0;
	
}


char* getwlanip(char* wip_buf)
{
    struct ifreq wtemp;
    struct sockaddr_in *wmyaddr;
    int wfd = 0;
    int wret = -1;
    strcpy(wtemp.ifr_name, "wlan0");
    if((wfd=socket(AF_INET, SOCK_STREAM, 0))<0)
    {
        // return NULL;
      strcpy(wip_buf, "0.0.0.0");
     // printf("WLAN IP: %s\n", wip_buf);
      return wip_buf;
    } else
    {
    wret = ioctl(wfd, SIOCGIFADDR, &wtemp);
    close(wfd);
    if(wret < 0) {
       // return NULL;
        strcpy(wip_buf, "0.0.0.0");
      //   printf("WLAN IP: %s\n", wip_buf);
        return wip_buf;
                 } else
                 {
                wmyaddr = (struct sockaddr_in *)&(wtemp.ifr_addr);
                strcpy(wip_buf, inet_ntoa(wmyaddr->sin_addr));
        //      printf("WLAN IP: %s\n", wip_buf);
                return wip_buf;
                 }
    }
}

char* getPowerState(char* PowerSatateUB)
{

	if (PI_MODE) {	
        int     PoweState = 0;
        const int       PowIndPin1 = 4;
        if (wiringPiSetupGpio() == -1)
                {
                zlog_info(zc,"getPowerState:error in setup WiringPi!");
                exit(0);
                }

        pinMode(PowIndPin1,INPUT);

        PoweState = digitalRead(PowIndPin1);

        zlog_info(zc,"getPowerState:Pin %d is:%d ",PowIndPin1,PoweState);

        if (PoweState ==0 ) { zlog_info(zc,"getPowerState: Power with Battery (%d).",PoweState);
                              return("B");
                            }
                else    { zlog_info(zc,"getPowerState: Power with USB(%d).",PoweState);
                              return("U");
                        }
	}
	return("U");
}

float getVCell()
{
	float batcell = 0.0;
	if (PI_MODE) {

    int fd = wiringPiI2CSetup(MAX17040_ADDRESS);
    if (fd == -1) {
        zlog_info(zc,"getVCell:Failed to init I2C comm. ");
        return(0);
                }
        else {
    zlog_info(zc,"getVCell:I2C comm. successfully. ");
             }

    int value = Swap2Bytes(wiringPiI2CReadReg16(fd,VCELL_REGISTER));

    zlog_info(zc,"getVCell:the value read from reg is: %x ",value);

    batcell=(value*0.00125)/16;

    zlog_info(zc,"getVCell:Vcell is %f ",batcell);
	} else {
	batcell = 0.0;
	}

    return(batcell);

}

float getSoC()
{
	    float  value=0;
	if (PI_MODE) {

    int fd = wiringPiI2CSetup(MAX17040_ADDRESS);
    if (fd == -1) {
        zlog_info(zc,"getSoC: Failed to init I2C comm.");
        return(0);
                }
        else {
    zlog_info(zc,"getSoC: I2C comm. successfully. ");
             }

    int value1 = wiringPiI2CReadReg8(fd,SOC_REGISTER);
    int value2 = wiringPiI2CReadReg8(fd,(SOC_REGISTER+1));


    zlog_info(zc,"getSoC:the value read from soc_regs are: %x,%x ",value1,value2);

    value=value1+(value2/256.0);

    zlog_info(zc,"getSoC:Battery Capacity is %f ",value);
	} else {
	value = 0.0;
	}
    return(value);

}

void quickStart()
{

	if (PI_MODE) {

    int fd = wiringPiI2CSetup(MAX17040_ADDRESS);
    if (fd == -1) {
        zlog_info(zc,"quickStart: Failed to init I2C comm.");
                }
        else {
    	zlog_info(zc,"quickStart: I2C comm. successfully. ");
             }

/*    wiringPiI2CWriteReg8(fd,COMMAND_REGISTER,0x00);
    wiringPiI2CWriteReg8(fd,COMMAND_REGISTER+1,0x54);
*/

      wiringPiI2CWriteReg16(fd,COMMAND_REGISTER,0x0054);


/*    wiringPiI2CWriteReg8(fd,MODE_REGISTER,0x40);
    wiringPiI2CWriteReg8(fd,MODE_REGISTER+1,0x00);
 */

      wiringPiI2CWriteReg16(fd,MODE_REGISTER,0x4000);
    zlog_info(zc,"quickStart: Power on Reset and Quick Start set.");
	}

        sleep(10);

}

void power_down_sys()
{

        printf("Rcved halt_sys Command from administor, System is downing in 5 seconds......\n");
	zlog_info(zc,"power_down_sys: Rcved halt_sys Command from administor, System is downing in 5 seconds.....");

        sleep(4);

        sync();
        reboot(LINUX_REBOOT_CMD_POWER_OFF);

}

float Get_Cpu_Temperature(void)
{
    int fd;
    double temp = 0;
    char buf[MAX_SIZE];

    // open/sys/class/thermal/thermal_zone0/temp
    fd = open(TEMP_FILE_PATH, O_RDONLY);
    if (fd < 0) {
        zlog_info(zc,"Get_Cpu_Temperature: failed to open thermal_zone0/temp");
        return -1;
    }

    // read temperature
    if (read(fd, buf, MAX_SIZE) < 0) {
        zlog_info(zc, "Get_Cpu_Temperature:failed to read temp");
        return -1;
    }

    // print as float
    temp = atoi(buf) / 1000.0;

    // close file
    close(fd);
    return temp;
}

void  Collect_CreatjSON(struct pubsub_opts_struct* opts)
{

	
        float   Shutdown_th;
        char*   UB;
        float   battery_v;
	float	randub;
        char    *js = NULL ;
        char    *al = NULL;

	stat_collect = stat_collect + 1;
	
	srand(time(0));


        js= (char *)malloc(300);
        al= (char *)malloc(15);

        strcpy(al,"Normal");

        cJSON* report_body = NULL;
        cJSON* dp_body = NULL;
        cJSON* data_set = NULL;
        cJSON* items_in_set =NULL;

        report_body = cJSON_CreateObject();
        dp_body = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddNumberToObject(report_body,"id",888);

        zlog_info(zc,"Collect_CreatjSON:~Sys info Load into jSON~");

        //system usage
        struct sysinfo sys_info;
        if(sysinfo(&sys_info) != 0)
        {
        zlog_info(zc,"Collect_CreatjSON:sysinfo-Error");
        }

        //uptime
        unsigned long uptime = sys_info.uptime / 60;
        zlog_info(zc,"Collect_CreatjSON: Uptime %ld min.", uptime);

        items_in_set = cJSON_CreateObject();

        cJSON_AddNumberToObject(items_in_set, "v",uptime);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"Up_Time",data_set);


        //cpu usage
        unsigned long avgCpuLoad = sys_info.loads[0] / 1000;
        zlog_info(zc,"Collect_CreatjSON: CPU  %ld%%", avgCpuLoad);
        if (avgCpuLoad >= 70 ) {
                strcpy(al,"Critical"); }

        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddNumberToObject(items_in_set, "v",avgCpuLoad);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"CPU_Ut",data_set);


         //ram usgae
        unsigned long totalRam = sys_info.totalram / 1024 / 1024;
        unsigned long freeRam = sys_info.freeram /1024 /1024;
        unsigned long usedRam = totalRam - freeRam;
        unsigned long ram_load = (usedRam * 100) / totalRam;
        zlog_info(zc,"Collect_CreatjSON: RAM %d, %d ,%d ", totalRam, usedRam, ram_load);

        if (ram_load >=88 ) {
                strcpy(al,"Critical"); }


        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();


        cJSON_AddNumberToObject(items_in_set, "v",ram_load);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"RAM_Ut",data_set);

        //cpu Temperature
	float cpu_temperature_Result;
	if (PI_MODE) {
        cpu_temperature_Result = Get_Cpu_Temperature();
	} else {
		cpu_temperature_Result = (35 + 1.0*(rand()%RAND_MAX)/RAND_MAX *(51-35));
	}
        zlog_info(zc,"Collect_CreatjSON: TEMP %.2f C", cpu_temperature_Result);

        if (cpu_temperature_Result >=50 ) {
                        strcpy(al,"Critical"); }


        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddNumberToObject(items_in_set, "v",cpu_temperature_Result);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"cpu_temp",data_set);


        //IP address
        char ipInfo[16];
        struct ifreq temp;
        struct sockaddr_in *myaddr;
        int fd = 0;
        int ret = -1;
        strcpy(temp.ifr_name, "eth0");

        if((fd=socket(AF_INET, SOCK_STREAM, 0))<0)
        {
            zlog_info(zc,"Collect_CreatjSON: get eth0 ip address-Error");
            strcpy(ipInfo, "0.0.0.0");
         }else {
        ret = ioctl(fd, SIOCGIFADDR, &temp);
        close(fd);
        if(ret < 0) { printf("Get address Error.\n");

                        strcpy(ipInfo,"0.0.0.0");
                         } else {

                myaddr = (struct sockaddr_in *)&(temp.ifr_addr);
                strcpy(ipInfo, inet_ntoa(myaddr->sin_addr));
                        }
                }
        zlog_info(zc,"Collect_CreatjSON: eth0 IP: %s ", ipInfo);


        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddStringToObject(items_in_set, "v",ipInfo);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"IP",data_set);


        //wlan address
        char  wipinfo[16];
        getwlanip(wipinfo);
        zlog_info(zc,"Collect_CreatjSON: wlan0 IP: %s", getwlanip(wipinfo));

	/* quickStart(); */
	
	randub = (0+1.0*(rand()%RAND_MAX)/RAND_MAX *(1-0));

	if (PI_MODE) {
        char PwerS[1];
        UB=getPowerState(PwerS);
	} else {
		if (randub >0.9 || randub <0.1) {
		UB = "B"; 
			} else {
			UB="U";
			}
	}
	zlog_info(zc,"Collect_CreatjSON: Current Power Status is :%s .",UB);


        if ( strcmp(UB,"B")==0 ) {
                     strcpy(al,"Critical"); }

        items_in_set =NULL;

        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();


        cJSON_AddStringToObject(items_in_set, "v",UB);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"Ext_P",data_set);

	if (PI_MODE) {
        battery_v = getVCell();
	} else
	{
	  battery_v = (2.5+ 1.0*(rand()%RAND_MAX)/RAND_MAX *(4.2-2.5));
	}
        zlog_info(zc,"Collect_CreatjSON: Vcell is %f v ",battery_v);

        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddNumberToObject(items_in_set, "v",battery_v);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"Battery_Volt",data_set);

	if (PI_MODE) {
        Shutdown_th = getSoC();
	} else {
	Shutdown_th = (32 + 1.0*(rand()%RAND_MAX)/RAND_MAX *(60-32));
	}

        zlog_info(zc,"Collect_CreatjSON: Battery capacity is: %f",Shutdown_th);


        items_in_set =NULL;
        data_set = NULL;
        items_in_set = cJSON_CreateObject();
        data_set = cJSON_CreateArray();

        cJSON_AddNumberToObject(items_in_set, "v",Shutdown_th);
        cJSON_AddItemToArray(data_set,items_in_set);
        cJSON_AddItemToObject(dp_body,"Battery_Cap",data_set);

        if ( strcmp(UB, "B")==0 ) {
                if ( Shutdown_th<=PowerDown_th) {
                         strcpy(al,"Critical");

                        }
                }



        cJSON_AddItemToObject(report_body,"dp",dp_body);

        /*	printf("Data for json payload: %s\n",cJSON_Print(report_body));	*/
	zlog_info(zc,"Collect_CreatjSON: Data for json payload: %s",cJSON_Print(report_body));

        opts->payload_message = strdup(cJSON_PrintUnformatted(report_body));
	
	opts->alarm_level = strdup(al);

        cJSON_Delete(report_body);

	free(js);
	js = NULL;
	free(al);
	al = NULL;

}

/*Just support 6  cmds from administor	*/
/*	start_report			*/
/*	stop_report			*/
/*	reduce_freq			*/
/*	increase_freq			*/
/*	halt_sys			*/
/*	Notes:				*/
char* executeRemoteCmd(char* cmdString, struct pubsub_opts_struct* opts, void* context)
{
	
	int n;
	
	char *cmd_respond_str = "Get cmd and to be process...";

	zlog_info(zc,"executeRemoteCmd: Enter exec rmt cmd section with  [%s]", cmdString);
	
	stat_cmd = stat_cmd + 1;

	if(strcasecmp("start_report", cmdString) == 0) {
                        opts->stop_publish = 0;
                        /* printf(zc,"executed cmd: start publish.\n");	*/
                        n = ini_putl(opts->clientid, "Stop_Publish", opts->stop_publish, Config_File);
                        zlog_info(zc,"executeRemoteCmd: Start publish setting and stored to file.\n");
			cmd_respond_str = "Executed cmd: start publishment.";

		}
		else if(strcasecmp("stop_report", cmdString) == 0) {
			opts->stop_publish = 1;
		/*	printf("Executed cmd: stop publish.\n");	*/
                        n = ini_putl(opts->clientid, "Stop_Publish", opts->stop_publish, Config_File);
                        zlog_info(zc,"executeRemoteCmd: Stop publish setting and stored to file.\n");
			cmd_respond_str = "Executed cmd: stop publishment.";

		}
		else if(strcasecmp("reduce_freq", cmdString) == 0) {
			
			if (opts->current_interval < 7200 ) {	
			opts->current_interval = opts->current_interval + 120;		
			opts->stop_publish = 0;
			zlog_info(zc,"executeRemoteCmd: Executed cmd. Current publish interval increase 120 seconds to %d with pub flag %d\n",opts->current_interval,opts->stop_publish);
			cmd_respond_str = "Executed cmd. Current publish interval increased additional 120 seconds.";
			} else {
				opts->current_interval = 7200 ;
				opts->stop_publish = 0;
				zlog_info(zc,"executeRemoteCmd：Executed cmd. Current publish interval set to %d [MAX] with pub flag %d\n",opts->current_interval,opts->stop_publish);
				cmd_respond_str = "Executed cmd. Current publish interval set to [7200s]MAX."; 
				}
			n = ini_putl(opts->clientid, "Last_Interval_Setting", opts->current_interval, Config_File);
			zlog_info(zc,"Interval setting (reduce frequ） store to file.");	

		}
		else if(strcasecmp("increase_freq", cmdString) == 0) {
			if ( strcasecmp(opts->alarm_level,"Critical") != 0) {
				if ( opts->current_interval > opts->critical_deta_time ) {
					opts->current_interval = opts->current_interval - 20;
					opts->stop_publish = 0;
					zlog_info(zc,"executeRemoteCmd:Executed cmd. Current publish interval decrease 20  seconds to %d with pub flag %d.",opts->current_interval,opts->stop_publish);
					cmd_respond_str = "Executed cmd. Current publish interval decreased 20  seconds.";
					} else {
					opts->current_interval = opts->critical_deta_time;
					opts->stop_publish = 0;
					zlog_info(zc,"executeRemoteCmd: Executed cmd. Current publish interval set to critical model[%d] with pub flag %d.",opts->current_interval,opts->stop_publish);
					cmd_respond_str = "Executed cmd. Current publish interval set to critical model[60s]MIN.";
					}
					
				}
			n = ini_putl(opts->clientid, "Last_Interval_Setting", opts->current_interval, Config_File);
			zlog_info(zc,"executeRemoteCmd: Interval setting (increase_frequ)  store to file.");

		}

		else if(strcasecmp("halt_sys", cmdString) == 0) {
		cmd_respond_str = "Executed cmd [halt_sys], but without permission!";	
		zlog_info(zc,"executeRemoteCmd, execute to halt_sys!");
		destoryandExit(context); 
		
		}
		else if(strstr(cmdString,"Notes:") != 0) {
			
			printf(BLINK"Notes from administor: "UNDERLINE"[%s]. \n"NONE,cmdString);
			zlog_info(zc,"executeRemoteCmd: Notes from administor: [%s]. ",cmdString);
			cmd_respond_str = "Notes from administor rcvd"; 

		}else {
		cmd_respond_str = "Rcved Cmd cannot be identificated"; 
		zlog_info(zc,"executeRemoteCmd: Rcved Cmd: [%s] is not supported!", cmdString) ;
		
		stat_invalidcmd = stat_invalidcmd + 1;
		}
	zlog_info(zc,"executeRemoteCmd: cmd executed, [%s] ",cmd_respond_str);
	return cmd_respond_str;
}


void destoryandExit(void *context)
{

        time_t EndTime = 0;
        char EndTimeStr[42] = {'\0'};

        struct tm *endTimeSt = NULL;

        EndTime = time(NULL);
        endTimeSt= localtime(&EndTime);
        sprintf(EndTimeStr, "%04d-%02d-%02d/%02d:%02d:%02d-(before halt-sys)", \
                endTimeSt->tm_year + 1900, endTimeSt->tm_mon + 1, \
                endTimeSt->tm_mday, endTimeSt->tm_hour, \
                endTimeSt->tm_min, endTimeSt->tm_sec);

        OutPutStatReport(common_opts.start_time_str,EndTimeStr);

/*	finished = 1;
	exitmainloop = 1;
	sleep(2);
*/
	power_down_sys();
	
	return;

}

void OutPutStatReport(char *startt, char *endt)
{
	FILE *fP;

	if((fP=fopen(STATFILENAME,"a+"))==NULL){
                zlog_info(zc,"Creat stat TEXTfile failed\n");
                return;
                }

        fprintf(fP,"Device Running Report                                   \n");
        fprintf(fP,"Start:%s\t\tEnd:%s                                      \n",startt,endt);
        fprintf(fP,"Items:\tConnection\tConn_Lost\tConn_fail                \n");
        fprintf(fP,"Data:\t%ld\t%ld\t%ld                                    \n",stat_con,stat_conlos,stat_confail);
        fprintf(fP,"Items:\tpublish\tpublish_fail\tpubacp\tpubdeny          \n");
        fprintf(fP,"Data:\t%ld\t%ld\t%ld\t%ld                               \n",stat_pub,stat_pubfail,stat_pubacp,stat_pubdeny);
        fprintf(fP,"Items:\tsubscribe\tsubscribe_fail                       \n");
        fprintf(fP,"Data:\t%ld\t%ld                                 \n",stat_sub,stat_subfail);
        fprintf(fP,"Items:\tdisconnection\tdisconn_fail                     \n");
        fprintf(fP,"Data:\t%ld\t%ld                                 \n",stat_disconn,stat_disconnfail);
        fprintf(fP,"Items:\tCmd_recvd\tinvalid_cmd                          \n");
        fprintf(fP,"Data:\t%ld\t%ld                                 \n",stat_cmd,stat_invalidcmd);
        fprintf(fP,"Items:\tmsg_ecvd\tmsd_sent                              \n");
        fprintf(fP,"Data:\t%ld\t%ld                                 \n",stat_msgrcvd,stat_msgsend);
        fprintf(fP,"Items:\tCollect\tloops                                  \n");
        fprintf(fP,"Data:\t%ld\t%ld                                 \n",stat_collect,stat_mainloop);
        fprintf(fP,"End report.                                     \n");
	fclose(fP);


        fprintf(stdout,"Device Running Report                                   \n");
        fprintf(stdout,"Start:%s\t\tEnd:%s                                      \n",startt,endt);
        fprintf(stdout,"Items:\tConnection\tConn_Lost\tConn_fail                \n");
        fprintf(stdout,"Data:\t%ld\t%ld\t%ld                                    \n",stat_con,stat_conlos,stat_confail);
        fprintf(stdout,"Items:\tpublish\tpublish_fail\tpubacp\tpubdeny          \n");
        fprintf(stdout,"Data:\t%ld\t%ld\t%ld\t%ld                               \n",stat_pub,stat_pubfail,stat_pubacp,stat_pubdeny);
        fprintf(stdout,"Items:\tsubscribe\tsubscribe_fail                       \n");
        fprintf(stdout,"Data:\t%ld\t%ld                                 \n",stat_sub,stat_subfail);
        fprintf(stdout,"Items:\tdisconnection\tdisconn_fail                     \n");
        fprintf(stdout,"Data:\t%ld\t%ld                                 \n",stat_disconn,stat_disconnfail);
        fprintf(stdout,"Items:\tCmd_recvd\tinvalid_cmd                          \n");
        fprintf(stdout,"Data:\t%ld\t%ld                                 \n",stat_cmd,stat_invalidcmd);
        fprintf(stdout,"Items:\tmsg_ecvd\tmsd_sent                              \n");
        fprintf(stdout,"Data:\t%ld\t%ld                                 \n",stat_msgrcvd,stat_msgsend);
        fprintf(stdout,"Items:\tCollect\tloops                                  \n");
        fprintf(stdout,"Data:\t%ld\t%ld                                 \n",stat_collect,stat_mainloop);
        fprintf(stdout,"End report.                                     \n");
}
