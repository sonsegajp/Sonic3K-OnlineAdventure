// Compiled only for local integration testing. No test socket in release builds.
#include <winsock2.h>
#include <string>
static SOCKET test_listener=INVALID_SOCKET,test_client=INVALID_SOCKET;
static std::string test_rx,test_tx;
static std::vector<std::string> test_native_queue;
static bool test_native_applying=false;
static bool test_setup;
static uint16_t test_pad;
static bool test_pad_set;
static unsigned test_frame_address;
static std::string test_frame_hex;
static void test_frame_write(){
    for(size_t i=0;i<test_frame_hex.size();i+=2)host.write8(test_frame_address+unsigned(i/2),uint8_t(strtoul(test_frame_hex.substr(i,2).c_str(),nullptr,16)));
    test_frame_hex.clear();
}
static uint64_t test_ticks;
static double test_deadline;
static float test_fps;
static void test_pace() {
    static bool configured;
    if(!configured){configured=true;const char *v=getenv("OA_TEST_FPS");test_fps=v?float(atof(v)):0;}
    if(test_fps<=0||test_fps>240)return;
    double frequency=double(SDL_GetPerformanceFrequency()),now=double(SDL_GetPerformanceCounter());
    double period=frequency/test_fps;
    if(test_deadline==0||now-test_deadline>period*3)test_deadline=now;
    test_deadline+=period;
    while((now=double(SDL_GetPerformanceCounter()))<test_deadline){double ms=(test_deadline-now)*1000/frequency;SDL_Delay(Uint32(ms>1?ms:1));}
}
static std::string field(const std::string &j,const char *key) {
    size_t p=j.find(std::string("\"")+key+"\"");if(p==std::string::npos)return {};
    p=j.find(':',p);if(p==std::string::npos)return {};p=j.find_first_not_of(" \t",p+1);if(p==std::string::npos)return {};
    bool quoted=j[p]=='"';if(quoted)p++;
    size_t e=j.find_first_of(quoted?"\"":",} \r\n",p);return j.substr(p,e-p);
}
static void test_send(const char *line) {if(test_client!=INVALID_SOCKET){test_tx+=line;test_tx+='\n';}}
static void test_command(const std::string &j) {
    int id=atoi(field(j,"id").c_str());auto cmd=field(j,"cmd");char out[2048];
    // A host video tick can interrupt player physics. Test setup must wait
    // for the next native loop, and acknowledge only after the write executes.
    if(!test_native_applying&&field(j,"native")=="1"&&g_ram[0xf600]==0x0c&&g_ram[0xf711]&&
       (cmd=="write_memory"||(cmd=="coop"&&(field(j,"action")=="test_warp"||field(j,"action")=="test_contact")))){
        test_native_queue.push_back(j);return;
    }
    if(cmd=="coop") {mod->debug(id,j.c_str());return;}
    if(cmd=="test_stats"){snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true,\"ticks\":%llu,\"ms\":%u,\"fps_limit\":%.5f,\"sp\":%u,\"rte\":%d,\"split\":%d}",id,(unsigned long long)test_ticks,SDL_GetTicks(),test_fps,g_cpu.A[7],g_rte_pending,g_split_sp_popped);test_send(out);return;}
    if(cmd=="read_ram") {
        unsigned a=strtoul(field(j,"addr").c_str(),nullptr,0)&65535;int n=atoi(field(j,"size").c_str());
        if(n<0||n>65536||a+unsigned(n)>65536){error(id,"Invalid read range");return;}
        std::string hex;hex.reserve(n*2);for(int i=0;i<n;i++){char b[3];snprintf(b,3,"%02x",g_ram[a+i]);hex+=b;}
        test_send(("{\"id\":"+std::to_string(id)+",\"ok\":true,\"hex\":\""+hex+"\"}").c_str());return;
    }
    if(cmd=="write_memory"||cmd=="write_frame") {
        unsigned a=strtoul(field(j,"addr").c_str(),nullptr,0);auto hex=field(j,"hex");
        if(a<0xff0000||a+hex.size()/2>0x1000000||hex.size()%2){error(id,"Invalid RAM write");return;}
        if(cmd=="write_frame"){test_frame_address=a;test_frame_hex=hex;}
        else for(size_t i=0;i<hex.size();i+=2)host.write8(a+unsigned(i/2),uint8_t(strtoul(hex.substr(i,2).c_str(),nullptr,16)));
    } else if(cmd=="load_stage") {
        unsigned stage=strtoul(field(j,"stage").c_str(),nullptr,0);
        if(stage>0x1701||(stage&255)>1){error(id,"Invalid native stage");return;}
        // Request a normal restart now, but change level IDs only at Level's
        // entry. A frame can yield midway through ScreenEvents; changing IDs
        // there would run the new zone's script with the previous zone's state.
        test_pending_stage=stage;host.write16(0xfffe02,1);
    } else if(cmd=="show_window") {if(auto *window=SDL_GetWindowFromID(1)){SDL_ShowWindow(window);SDL_RaiseWindow(window);}}
    else if(cmd=="release_input") {test_pad=0;test_pad_set=false;}
    else if(cmd=="set_input") {test_pad=uint16_t(strtoul(field(j,"keys").c_str(),nullptr,16));test_pad_set=true;}
    else if(cmd=="sonic_state") {
        unsigned x=(g_ram[0xb010]<<8)|g_ram[0xb011],y=(g_ram[0xb014]<<8)|g_ram[0xb015];
        snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true,\"char_id\":%u,\"x\":%u,\"y\":%u,\"routine\":%u,\"mode\":%u}",id,g_ram[0xb038],x,y,g_ram[0xb005],g_ram[0xf600]);test_send(out);return;
    } else if(cmd=="quit") {SDL_Event e{};e.type=SDL_QUIT;SDL_PushEvent(&e);}
    else {error(id,"Unknown test command");return;}
    snprintf(out,sizeof out,"{\"id\":%d,\"ok\":true}",id);test_send(out);
}
static void test_native_apply(){
    if(test_native_queue.empty())return;
    auto commands=std::move(test_native_queue);test_native_queue.clear();
    test_native_applying=true;for(const auto &command:commands)test_command(command);test_native_applying=false;
}
static void test_poll() {
    if(!mod)return;
    test_pace();test_ticks++;
    if(!test_setup) {
        test_setup=true;const char *port=getenv("OA_TEST_PORT");if(!port)return;
        int number=atoi(port);if(number<1024||number>65535)return;
        WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))return;
        test_listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(test_listener==INVALID_SOCKET)return;
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((u_short)number);
        if(bind(test_listener,(sockaddr*)&a,sizeof a)||listen(test_listener,1)){closesocket(test_listener);test_listener=INVALID_SOCKET;return;}
        u_long nonblock=1;ioctlsocket(test_listener,FIONBIO,&nonblock);
        fprintf(stderr,"[Online Adventure TEST] Local controls on 127.0.0.1:%d\n",number);
    }
    if(test_listener==INVALID_SOCKET)return;
    if(test_client==INVALID_SOCKET) {test_client=accept(test_listener,nullptr,nullptr);if(test_client==INVALID_SOCKET)return;u_long n=1;ioctlsocket(test_client,FIONBIO,&n);}
    char b[8192];int got=recv(test_client,b,sizeof b,0);
    if(got==0||(got<0&&WSAGetLastError()!=WSAEWOULDBLOCK)){closesocket(test_client);test_client=INVALID_SOCKET;test_rx.clear();test_tx.clear();return;}
    if(got>0)test_rx.append(b,got);
    if(test_rx.size()>131072){closesocket(test_client);test_client=INVALID_SOCKET;test_rx.clear();test_tx.clear();return;}
    size_t pos;while((pos=test_rx.find('\n'))!=std::string::npos){auto command=test_rx.substr(0,pos);test_rx.erase(0,pos+1);test_command(command);}
    if(!test_tx.empty()){int n=send(test_client,test_tx.data(),(int)test_tx.size(),0);if(n>0)test_tx.erase(0,n);}
}
