// Headless Lua host. Gameplay, renderer and frame construction remain in script.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
static_assert(sizeof(lua_Integer)==8 && sizeof(lua_Number)==8,"int64/double Lua required");
#ifndef JAIDOOM_LUA_SCRIPTS
#define JAIDOOM_LUA_SCRIPTS ""
#endif
#ifndef JAIDOOM_GAME_ROOT
#define JAIDOOM_GAME_ROOT ""
#endif
namespace {
struct options {
	std::filesystem::path scripts{JAIDOOM_LUA_SCRIPTS},game{JAIDOOM_GAME_ROOT},trace;
	std::string entry{"p10_timedemo"};
	std::vector<std::string> args;
	int repeats{0},limit{0};
};
struct session {
	options opt;std::filesystem::path binary;
	std::mt19937_64 rng{0x9E3779B97F4A7C15ull};
	std::ofstream trace;
	uint64_t frame_hash{14695981039346656037ull};int phase{0};
};
session& ctx(lua_State* l){return *static_cast<session*>(lua_touserdata(l,lua_upvalueindex(1)));}
std::string read(const std::filesystem::path& path){
	std::ifstream in(path,std::ios::binary);if(!in){throw std::runtime_error("Cannot read "+path.string());}
	std::ostringstream result;result<<in.rdbuf();return result.str();
}
int push(lua_State* l,const std::string& s){lua_pushlstring(l,s.data(),s.size());return 1;}
int read_file(lua_State* l){return push(l,read(luaL_checkstring(l,1)));}
int write_file(lua_State* l){
	auto path=std::filesystem::path(luaL_checkstring(l,1));size_t n{0};auto data=luaL_checklstring(l,2,&n);
	std::ofstream out(path,std::ios::binary);if(!out){throw std::runtime_error("Cannot write "+path.string());}
	out.write(data,static_cast<std::streamsize>(n));return 0;
}
int file_exists(lua_State* l){lua_pushboolean(l,std::filesystem::is_regular_file(luaL_checkstring(l,1)));return 1;}
int dir_exists(lua_State* l){lua_pushboolean(l,std::filesystem::is_directory(luaL_checkstring(l,1)));return 1;}
int parent_dir(lua_State* l){return push(l,std::filesystem::path(luaL_checkstring(l,1)).parent_path().generic_string());}
int file_name(lua_State* l){return push(l,std::filesystem::path(luaL_checkstring(l,1)).filename().generic_string());}
int root_dir(lua_State* l){return push(l,(ctx(l).opt.game/"scripts").generic_string());}
int bin_dir(lua_State* l){return push(l,ctx(l).binary.parent_path().generic_string());}
int data_dir(lua_State* l){return push(l,ctx(l).opt.game.generic_string());}
int clock_ms(lua_State* l){
	auto now=std::chrono::steady_clock::now().time_since_epoch();
	lua_pushinteger(l,std::chrono::duration_cast<std::chrono::milliseconds>(now).count());return 1;
}
int integer(lua_State* l){
	if(lua_isinteger(l,1)){lua_pushvalue(l,1);return 1;}
	if(lua_isboolean(l,1)){lua_pushinteger(l,lua_toboolean(l,1)?1:0);return 1;}
	double v=luaL_checknumber(l,1);
	if(!std::isfinite(v)||v<-9223372036854775808.0||v>=9223372036854775808.0){throw std::runtime_error("int out of range");}
	lua_pushinteger(l,static_cast<lua_Integer>(v));return 1;
}
int ifloor(lua_State* l){
	double v=std::floor(luaL_checknumber(l,1));
	if(!std::isfinite(v)||v<-9223372036854775808.0||v>=9223372036854775808.0){throw std::runtime_error("ifloor out of range");}
	lua_pushinteger(l,static_cast<lua_Integer>(v));return 1;
}
int random_seed(lua_State* l){ctx(l).rng.seed(static_cast<uint64_t>(luaL_checkinteger(l,1)));return 0;}
int random_value(lua_State* l){
	auto& rng=ctx(l).rng;
	if(lua_gettop(l)==0){lua_pushnumber(l,static_cast<double>(rng()>>11)*(1.0/9007199254740991.0));return 1;}
	if(lua_gettop(l)!=2){throw std::runtime_error("random expects zero or two arguments");}
	if(lua_isinteger(l,1)&&lua_isinteger(l,2)){
		auto lo=lua_tointeger(l,1),hi=lua_tointeger(l,2);if(hi<lo){throw std::runtime_error("inverted random range");}
		uint64_t span=static_cast<uint64_t>(hi)-static_cast<uint64_t>(lo)+1,value=rng();if(span){value%=span;}
		lua_pushinteger(l,static_cast<int64_t>(static_cast<uint64_t>(lo)+value));return 1;
	}
	double lo=luaL_checknumber(l,1),hi=luaL_checknumber(l,2);if(hi<lo){throw std::runtime_error("inverted random range");}
	lua_pushnumber(l,lo+static_cast<double>(rng()>>11)*(1.0/9007199254740991.0)*(hi-lo));return 1;
}
int checkpoint(lua_State* l){
	auto& c=ctx(l);auto tic=luaL_checkinteger(l,1),state=luaL_checkinteger(l,2);size_t n{0};auto frame=luaL_checklstring(l,3,&n);
	for(size_t i{0};i<n;++i){c.frame_hash^=static_cast<unsigned char>(frame[i]);c.frame_hash*=1099511628211ull;}
	if(tic==1&&c.phase==0){std::ofstream raw(c.opt.trace.string()+".frame1",std::ios::binary);raw.write(frame,n);}
	if(c.trace.is_open()){c.trace<<c.phase<<'\t'<<tic<<'\t'<<state<<'\t'<<n<<'\t'<<std::hex<<std::setw(16)<<std::setfill('0')<<c.frame_hash<<std::dec<<'\n';}
	return 0;
}
template<int(*Fn)(lua_State*)> int guarded(lua_State* l){
	try{return Fn(l);}catch(const std::exception& e){lua_pushstring(l,e.what());}
	return lua_error(l);
}
void bind(lua_State* l,session& c,const char* name,lua_CFunction fn){lua_pushlightuserdata(l,&c);lua_pushcclosure(l,fn,1);lua_setglobal(l,name);}
int traceback(lua_State* l){luaL_traceback(l,l,lua_tostring(l,1),1);return 1;}
void call(lua_State* l,int results=0){
	int base=lua_gettop(l);lua_pushcfunction(l,traceback);lua_insert(l,base);
	if(lua_pcall(l,0,results,base)!=LUA_OK){std::string s=lua_tostring(l,-1);lua_settop(l,base-1);throw std::runtime_error(s);}
	lua_remove(l,base);
}
void run(lua_State* l,const std::string& text,const std::string& file){
	if(luaL_loadbuffer(l,text.data(),text.size(),("@"+file).c_str())!=LUA_OK){std::string s=lua_tostring(l,-1);lua_pop(l,1);throw std::runtime_error(s);}
	call(l);
}
void help(){
#ifdef JAIDOOM_LAYOUT_PADDING
	volatile uint64_t padding{17};
	padding = (padding ^ 7ull) + 3ull;
	padding = (padding ^ 20ull) + 22ull;
	padding = (padding ^ 33ull) + 41ull;
	padding = (padding ^ 46ull) + 60ull;
	padding = (padding ^ 59ull) + 79ull;
	padding = (padding ^ 72ull) + 98ull;
	padding = (padding ^ 85ull) + 117ull;
	padding = (padding ^ 98ull) + 136ull;
	padding = (padding ^ 111ull) + 155ull;
	padding = (padding ^ 124ull) + 174ull;
	padding = (padding ^ 137ull) + 193ull;
	padding = (padding ^ 150ull) + 212ull;
	padding = (padding ^ 163ull) + 231ull;
	padding = (padding ^ 176ull) + 250ull;
	padding = (padding ^ 189ull) + 269ull;
	padding = (padding ^ 202ull) + 288ull;
	padding = (padding ^ 215ull) + 307ull;
	padding = (padding ^ 228ull) + 326ull;
	padding = (padding ^ 241ull) + 345ull;
	padding = (padding ^ 254ull) + 364ull;
	padding = (padding ^ 267ull) + 383ull;
	padding = (padding ^ 280ull) + 402ull;
	padding = (padding ^ 293ull) + 421ull;
	padding = (padding ^ 306ull) + 440ull;
	padding = (padding ^ 319ull) + 459ull;
	padding = (padding ^ 332ull) + 478ull;
	padding = (padding ^ 345ull) + 497ull;
	padding = (padding ^ 358ull) + 516ull;
	padding = (padding ^ 371ull) + 535ull;
	padding = (padding ^ 384ull) + 554ull;
	padding = (padding ^ 397ull) + 573ull;
	padding = (padding ^ 410ull) + 592ull;
	padding = (padding ^ 423ull) + 611ull;
	padding = (padding ^ 436ull) + 630ull;
	padding = (padding ^ 449ull) + 649ull;
	padding = (padding ^ 462ull) + 668ull;
	padding = (padding ^ 475ull) + 687ull;
	padding = (padding ^ 488ull) + 706ull;
	padding = (padding ^ 501ull) + 725ull;
	padding = (padding ^ 514ull) + 744ull;
	(void)padding;
#endif
	std::cout<<"jai_doom_lua --scripts DIR --game-root DIR --script p10_timedemo|p7_bench|p4_smoke\n"
		<<"  --repeat N (warm replay then N replays in the same state)\n"
		<<"  --trace FILE [--limit TICS] (conformance only)\n"
		<<"  -- DEMO.json 120x78\n";
}
options parse(int argc,char**argv){
	options result;
	for(int i=1;i<argc;++i){
		std::string a=argv[i];if(a=="--"){for(++i;i<argc;++i){result.args.emplace_back(argv[i]);}break;}
		auto next=[&](){if(++i>=argc){throw std::runtime_error("missing value for "+a);}return std::string(argv[i]);};
		if(a=="--scripts"){result.scripts=next();}else if(a=="--game-root"){result.game=next();}
		else if(a=="--script"){result.entry=next();}else if(a=="--repeat"){result.repeats=std::stoi(next());}
		else if(a=="--trace"){result.trace=next();}else if(a=="--limit"){result.limit=std::stoi(next());}
		else{throw std::runtime_error("unknown argument "+a);}
	}
	if(result.repeats<0||result.limit<0||(result.limit&&result.trace.empty())){throw std::runtime_error("invalid replay/trace options");}
	return result;
}
}
int main(int argc,char**argv){
	if(argc>1&&std::string(argv[1])=="--help"){help();return 0;}
	if(argc>1&&std::string(argv[1])=="--version"){std::cout<<LUA_RELEASE<<"\n";return 0;}
	lua_State* l=nullptr;
	try{
		session c;c.opt=parse(argc,argv);c.binary=std::filesystem::absolute(argv[0]);
		c.opt.scripts=std::filesystem::absolute(c.opt.scripts);c.opt.game=std::filesystem::absolute(c.opt.game);
		if(!c.opt.trace.empty()){c.trace.open(c.opt.trace,std::ios::binary);if(!c.trace){throw std::runtime_error("cannot open trace");}}
		l=luaL_newstate();if(!l){throw std::runtime_error("cannot create Lua state");}luaL_openlibs(l);
#define B(n,f) bind(l,c,n,guarded<f>)
		B("read_file",read_file);B("write_file",write_file);B("file_exists",file_exists);B("dir_exists",dir_exists);
		B("parent_dir",parent_dir);B("file_name",file_name);B("root_script_dir",root_dir);B("bin_dir",bin_dir);
		B("app_data_dir",data_dir);B("writable_dir",dir_exists);B("clock_ms",clock_ms);
		B("_j_int",integer);B("_j_ifloor",ifloor);B("_j_random",random_value);B("_j_random_seed",random_seed);B("_j_checkpoint",checkpoint);
#undef B
		run(l,read(c.opt.scripts/"runtime.lua"),(c.opt.scripts/"runtime.lua").string());
		lua_getglobal(l,"package");auto path=(c.opt.scripts/"?.lua").generic_string();lua_pushlstring(l,path.data(),path.size());lua_setfield(l,-2,"path");lua_pop(l,1);
		lua_createtable(l,static_cast<int>(c.opt.args.size()),0);
		for(size_t i{0};i<c.opt.args.size();++i){lua_pushlstring(l,c.opt.args[i].data(),c.opt.args[i].size());lua_rawseti(l,-2,i+1);}lua_setglobal(l,"ARGS");
		run(l,"ARGS=_j_array(ARGS);PI=math.pi;TAU=math.pi*2;E=math.exp(1)","host");
		auto entry=c.opt.scripts/(c.opt.entry+".lua");auto source=read(entry);
		if(c.trace.is_open()){
			if(c.opt.entry!="p10_timedemo"){throw std::runtime_error("trace requires p10_timedemo");}
			std::string marker="frame_bytes = (#frame)";auto at=source.find(marker);
			if(at==std::string::npos){throw std::runtime_error("trace insertion marker missing");}
			auto instrument=marker+"\n\t\t\t_j_checkpoint(GAME_TIC, demo_state_hash(), frame)";
			if(c.opt.limit){instrument+="\n\t\t\tif GAME_TIC >= "+std::to_string(c.opt.limit)+" then break end";}
			source.replace(at,marker.size(),instrument);
		}
		std::cout<<"JaiDOOM "<<LUA_RELEASE<<(c.trace.is_open()?" CONFORMANCE":"")<<"\n";
		if(c.opt.repeats){std::cout<<"JAI_BENCHMARK_PHASE warmup\n";}
		run(l,source,entry.string());
		lua_getglobal(l,"_jd_exit");int result=static_cast<int>(lua_tointeger(l,-1));lua_pop(l,1);
		if(result){lua_close(l);return result;}
		for(int i=0;i<c.opt.repeats;++i){
			c.phase=i+1;c.frame_hash=14695981039346656037ull;
			std::cout<<"JAI_BENCHMARK_PHASE measured "<<(i+1)<<"\n";
			lua_getglobal(l,"main");call(l,1);
			result=static_cast<int>(lua_tointeger(l,-1));lua_pop(l,1);
			std::cout<<"JAI_BENCHMARK_END "<<(i+1)<<" exit "<<result<<"\n";
			if(result){lua_close(l);return result;}
		}
		lua_close(l);return 0;
	}catch(const std::exception& e){std::cerr<<"jai_doom_lua: "<<e.what()<<"\n";if(l){lua_close(l);}return 1;}
}
