// Linux, C++17. Teaching benchmark: UDP userspace forwarding, not a PMD.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <poll.h>
#include <unistd.h>
#include <sched.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
using namespace std;
uint64_t now(){return chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now().time_since_epoch()).count();}
struct Header{uint64_t sent_ns,seq,rx_ns;uint32_t flow,magic;};
bool sampled(const Header& h,int n){uint64_t x=h.seq^(uint64_t(h.flow)<<32);x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;x^=x>>31;return x%uint64_t(n)==0;}
constexpr uint32_t MAGIC=0x4e465631;
struct Packet{char data[2048];int len;};
struct Stats{uint64_t packets=0,drop=0,errors=0,invalid=0;vector<double> latency;};
struct Queue{virtual bool push(Packet*)=0;virtual Packet* pop()=0;virtual ~Queue()=default;};
struct Shared:Queue{mutex m;deque<Packet*> q;size_t cap,peak=0;Shared(size_t c):cap(c){}bool push(Packet*p)override{lock_guard<mutex>l(m);if(q.size()==cap)return false;q.push_back(p);peak=max(peak,q.size());return true;}Packet*pop()override{lock_guard<mutex>l(m);if(q.empty())return nullptr;auto*p=q.front();q.pop_front();return p;}};
// One producer and one consumer ONLY. Acquire/release publishes initialized pointers.
struct SPSC:Queue{vector<Packet*>q;size_t cap;alignas(64)atomic<uint64_t>head{0};alignas(64)atomic<uint64_t>tail{0};uint64_t peak=0;
 SPSC(size_t c):q(c),cap(c){}bool push(Packet*p)override{auto h=head.load(memory_order_relaxed),t=tail.load(memory_order_acquire);if(h-t==cap)return false;q[h%cap]=p;head.store(h+1,memory_order_release);peak=max(peak,h+1-t);return true;}Packet*pop()override{auto t=tail.load(memory_order_relaxed),h=head.load(memory_order_acquire);if(t==h)return nullptr;auto*p=q[t%cap];tail.store(t+1,memory_order_release);return p;}};
map<string,string>opts;string get(string k,string d){return opts.count(k)?opts[k]:d;}int num(string k,int d){return stoi(get(k,to_string(d)));}
void pin(int i){auto cp=get("cpus","");if(cp.empty())return;vector<int> ids;size_t pos=0;while(pos<cp.size()){auto e=cp.find(',',pos);ids.push_back(stoi(cp.substr(pos,e-pos)));if(e==string::npos)break;pos=e+1;}if(i>=int(ids.size()))throw runtime_error("Not enough --cpus entries");cpu_set_t set;CPU_ZERO(&set);if(ids[i]<0||ids[i]>=CPU_SETSIZE)throw runtime_error("CPU out of range");CPU_SET(ids[i],&set);if(pthread_setaffinity_np(pthread_self(),sizeof(set),&set))throw runtime_error("CPU affinity failed");}
sockaddr_in addr(int port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);return a;}
int sock(int port=0){int fd=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);if(fd<0)throw runtime_error("socket");int n=4<<20;setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&n,sizeof(n));setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&n,sizeof(n));if(port){auto a=addr(port);if(bind(fd,(sockaddr*)&a,sizeof(a)))throw runtime_error("bind: port occupied");}return fd;}
void waitfd(int fd){pollfd p{fd,POLLIN,0};poll(&p,1,1);}
volatile uint64_t checksum_sink=0; // only assigned in main, after joining workers
uint64_t work(Packet*p,int n){uint64_t x=0;for(int i=0;i<n;i++)x=x*1664525+static_cast<unsigned char>(p->data[(i+sizeof(Header))%p->len])+1013904223;return x;}
double quant(vector<double>&v,double q){if(v.empty())return -1;sort(v.begin(),v.end());return v[size_t((v.size()-1)*q)];}
int main(int argc,char**argv){try{
 if(argc<2){cerr<<"send|forward|sink --key value ...\n";return 2;}string role=argv[1];for(int i=2;i<argc;i+=2){if(i+1==argc||string(argv[i]).rfind("--",0))throw runtime_error("Expected --key value");opts[string(argv[i]).substr(2)]=argv[i+1];}
 int S=num("threads",1),P=num("rx",1),W=num("workers",1),T=num("sinks",1),base=num("port",19000),out=num("out",20000),cap=num("capacity",1024),batch=num("batch",1),cost=num("work",0),bytes=num("bytes",256),sample=num("sample",64),flows=num("flows",64);double sec=stod(get("seconds","5"));uint64_t rate=stoull(get("rate","10000"));string model=get("model","rtc"),idle=get("idle","poll");
 if(S<1||P<1||(model!="rtc"&&W<1)||T<1||cap<1||batch<1||batch>256||bytes<int(sizeof(Header))||bytes>2048||sample<1||flows<1||sec<=0||rate<1||base<1024||out<1024||base+P>65535||out+max(T,S)>65535)throw runtime_error("Invalid option range");
 if(model!="rtc"&&model!="shared"&&model!="spsc")throw runtime_error("model rtc|shared|spsc");
 if(idle!="poll"&&idle!="wait")throw runtime_error("idle poll|wait");
 vector<thread> th;vector<Stats> st;vector<int> fds;vector<unique_ptr<Queue>>qs;atomic<int>ready{0};atomic<bool>start{false},done{false};uint64_t end=0,begin=0;int N=role=="forward"?P+(model=="rtc"?0:W):S;st.resize(N);vector<uint64_t>checks(N);rusage before{},after{};getrusage(RUSAGE_SELF,&before);
 auto gate=[&](int i){pin(i);ready.fetch_add(1);while(!start.load(memory_order_acquire))this_thread::yield();};
 if(role=="send"){
  for(int i=0;i<S;i++)fds.push_back(sock());
  for(int i=0;i<S;i++)th.emplace_back([&,i]{gate(i);uint64_t seq=0,next=begin,interval=uint64_t(1e9*double(S)/rate);Packet p{};p.len=bytes;memset(p.data,0x5a,bytes);while(now()<end){auto t=now();if(t<next){if(next-t>100000) this_thread::sleep_for(chrono::nanoseconds(next-t-50000));continue;}Header h{t,seq,0,uint32_t(i*flows+seq%flows),MAGIC};memcpy(p.data,&h,sizeof(h));auto a=addr(base+h.flow%P);if(sendto(fds[i],p.data,p.len,0,(sockaddr*)&a,sizeof(a))==p.len)st[i].packets++;else st[i].errors++;seq++;next+=max<uint64_t>(1,interval);if(t>next+10000000)next=t;}});
 }else if(role=="sink"){
  for(int i=0;i<S;i++)fds.push_back(sock(out+i));
  for(int i=0;i<S;i++)th.emplace_back([&,i]{gate(i);Packet p{};while(now()<end){int n=recv(fds[i],p.data,sizeof(p.data),0);if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)st[i].errors++;if(idle=="wait")waitfd(fds[i]);continue;}Header h{};if(n<int(sizeof(h))){st[i].invalid++;continue;}memcpy(&h,p.data,sizeof(h));if(h.magic!=MAGIC){st[i].invalid++;continue;}st[i].packets++;if(sampled(h,sample)&&st[i].latency.size()<1000000)st[i].latency.push_back((now()-h.sent_ns)/1000.0);}});
 }else if(role=="forward"){
  for(int i=0;i<P;i++)fds.push_back(sock(base+i));
  for(int i=0;i<(model=="rtc"?P:W);i++)fds.push_back(sock());
  if(model=="shared")qs.push_back(make_unique<Shared>(cap));else if(model=="spsc")for(int i=0;i<P*W;i++)qs.push_back(make_unique<SPSC>(cap));
  auto emit=[&](Packet*p,int i,int tx){checks[i]^=work(p,cost);Header h{};memcpy(&h,p->data,sizeof(h));auto a=addr(out+h.flow%T);if(sendto(tx,p->data,p->len,0,(sockaddr*)&a,sizeof(a))==p->len)st[i].packets++;else st[i].errors++;delete p;};
  for(int i=0;i<P;i++)th.emplace_back([&,i]{gate(i);while(now()<end){bool got=false;for(int b=0;b<batch;b++){auto*p=new Packet;int n=recv(fds[i],p->data,sizeof(p->data),0);if(n<0){delete p;if(errno!=EAGAIN&&errno!=EWOULDBLOCK)st[i].errors++;break;}got=true;p->len=n;Header h{};if(n<int(sizeof(h))){st[i].invalid++;delete p;continue;}memcpy(&h,p->data,sizeof(h));if(h.magic!=MAGIC){st[i].invalid++;delete p;continue;}h.rx_ns=now();memcpy(p->data,&h,sizeof(h));if(model=="rtc")emit(p,i,fds[P+i]);else{st[i].packets++;auto&q=qs[model=="shared"?0:i*W+h.flow%W];if(!q->push(p)){st[i].drop++;delete p;}}}if(!got&&idle=="wait")waitfd(fds[i]);}});
  if(model!="rtc")for(int j=0;j<W;j++)th.emplace_back([&,j]{int i=P+j;gate(i);int cursor=0;while(!done.load(memory_order_acquire)){bool got=false;for(int b=0;b<batch;b++){Packet*p=nullptr;if(model=="shared")p=qs[0]->pop();else{for(int k=0;k<P;k++){int r=(cursor+k)%P;p=qs[r*W+j]->pop();if(p){cursor=(r+1)%P;break;}}}if(!p)break;got=true;Header h{};memcpy(&h,p->data,sizeof(h));if(sampled(h,sample)&&st[i].latency.size()<1000000)st[i].latency.push_back((now()-h.rx_ns)/1000.0);emit(p,i,fds[P+j]);}if(!got&&idle=="wait")this_thread::sleep_for(chrono::microseconds(50));}});
 }else throw runtime_error("role send|forward|sink");
 while(ready.load()!=N)this_thread::yield();
 begin=now();end=begin+uint64_t(sec*1e9);start.store(true,memory_order_release);
 if(role=="forward"&&model!="rtc"){for(int i=0;i<P;i++)th[i].join();done.store(true,memory_order_release);for(int i=P;i<N;i++)th[i].join();}else for(auto&t:th)t.join();
 uint64_t residual=0;for(auto&q:qs){while(auto*p=q->pop()){residual++;delete p;}}getrusage(RUSAGE_SELF,&after);double elapsed=(now()-begin)/1e9;auto cpu=[](rusage r){return r.ru_utime.tv_sec+r.ru_utime.tv_usec/1e6+r.ru_stime.tv_sec+r.ru_stime.tv_usec/1e6;};uint64_t packets=0,rx=0,drop=0,errors=0,invalid=0,peak=0;vector<double>lat;
 for(int i=0;i<N;i++){bool output=role!="forward"||model=="rtc"||i>=P;if(output)packets+=st[i].packets;else rx+=st[i].packets;drop+=st[i].drop;errors+=st[i].errors;invalid+=st[i].invalid;lat.insert(lat.end(),st[i].latency.begin(),st[i].latency.end());checksum_sink^=checks[i];}
 for(auto&q:qs){if(auto*x=dynamic_cast<Shared*>(q.get()))peak=max<uint64_t>(peak,x->peak);if(auto*x=dynamic_cast<SPSC*>(q.get()))peak=max(peak,x->peak);}
 cout<<"{\"role\":\""<<role<<"\",\"model\":\""<<model<<"\",\"idle\":\""<<idle<<"\",\"threads\":"<<N<<",\"seconds\":"<<elapsed<<",\"packets\":"<<packets<<",\"rx_packets\":"<<rx<<",\"pps\":"<<packets/elapsed<<",\"queue_drop\":"<<drop<<",\"residual\":"<<residual<<",\"errors\":"<<errors<<",\"invalid\":"<<invalid<<",\"cpu_seconds\":"<<cpu(after)-cpu(before)<<",\"latency_kind\":\""<<(role=="sink"?"end_to_end":role=="forward"&&model!="rtc"?"rx_to_worker":"none")<<"\",\"latency_samples\":"<<lat.size()<<",\"p50_us\":"<<quant(lat,.5)<<",\"p99_us\":"<<quant(lat,.99)<<",\"queue_peak\":"<<peak<<",\"per_thread_packets\":[";for(int i=0;i<N;i++){if(i)cout<<',';cout<<st[i].packets;}cout<<"]}\n";for(int fd:fds)close(fd);return 0;
 }catch(exception&e){cerr<<e.what()<<'\n';return 2;}}
