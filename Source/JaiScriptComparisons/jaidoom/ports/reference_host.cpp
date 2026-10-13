// Conformance-only reference host: executes the original JaiDOOM scripts.
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
std::string read(const std::filesystem::path& path){
	std::ifstream in(path,std::ios::binary);if(!in){throw std::runtime_error("cannot read "+path.string());}
	std::ostringstream result;result<<in.rdbuf();return result.str();
}
int main(int argc,char**argv){
	if(argc<4){std::cerr<<"reference_host SCRIPT TRACE BACKEND [LIMIT] [REPEATS]\n";return 2;}
	try{
		auto script=std::filesystem::absolute(argv[1]);auto trace_path=std::filesystem::absolute(argv[2]);
		int limit=argc>4?std::stoi(argv[4]):0,repeats=argc>5?std::stoi(argv[5]):0,phase=0;
		std::ofstream trace(trace_path,std::ios::binary);if(!trace){throw std::runtime_error("cannot open trace");}
		uint64_t frame_hash=14695981039346656037ull;
		auto e=jai::engine::make();e->set_backend(std::string(argv[3])=="vm"?jai::backend_type::vm:jai::backend_type::interpreter);
		e->execution_budget(0);e->jaibite_cache(false);jai::stdlib::register_all(*e);
		e->add_function("read_file",[](const std::string& path){return read(path);});
		e->add_function("file_exists",[](const std::string& path){return std::filesystem::exists(path);});
		e->add_function("dir_exists",[](const std::string& path){return std::filesystem::is_directory(path);});
		e->add_function("writable_dir",[](const std::string& path){return std::filesystem::is_directory(path);});
		e->add_function("write_file",[](const std::string& path,const std::string& data){std::ofstream out(path,std::ios::binary);out.write(data.data(),data.size());return static_cast<bool>(out);});
		e->add_function("play_wav",[](const std::string&){return false;});
		e->add_function("play_midi",[](const std::string&){return false;});
		e->add_function("stop_midi",[](){});
		e->add_function("midi_status",[](){return std::string("stopped");});
		e->add_function("host_checkpoint",[&](jai::script_int tic,jai::script_int state,const std::string& frame){
			for(unsigned char c:frame){frame_hash^=c;frame_hash*=1099511628211ull;}
			trace<<phase<<'\t'<<tic<<'\t'<<state<<'\t'<<frame.size()<<'\t'<<std::hex<<std::setw(16)<<std::setfill('0')<<frame_hash<<std::dec<<'\n';
			if(tic==1&&phase==0){std::ofstream raw(trace_path.string()+".frame1",std::ios::binary);raw.write(frame.data(),frame.size());}
		});
		auto args=jai::script_value::make_array(nullptr,e.get());args.as_array().emplace_back((script.parent_path().parent_path()/"demo.json").generic_string(),e.get());args.as_array().emplace_back("120x78",e.get());e->add_global("ARGS",std::move(args));
		auto text=read(script);std::string marker="frame_bytes = frame.size();";auto pos=text.find(marker);
		if(pos==std::string::npos){throw std::runtime_error("checkpoint marker absent");}
		auto instrument=marker+"\n host_checkpoint(GAME_TIC, demo_state_hash(), frame);";
		if(limit){instrument+="\n if(GAME_TIC >= "+std::to_string(limit)+"){break;}";}
		text.replace(pos,marker.size(),instrument);
		(void)e->execute_file_source(script.generic_string(),text);
		for(int i=0;i<repeats;++i){phase=i+1;frame_hash=14695981039346656037ull;(void)e->execute("main();");}
		return 0;
	}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
