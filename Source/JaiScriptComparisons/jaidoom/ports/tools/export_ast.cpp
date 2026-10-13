// Export the workload's parsed syntax for the reviewed Lua port generator.
// This uses JaiScript's parser; it does not execute or change the game.
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string quote(std::string_view s) {
	std::string result{"\""};
	const char* digits="0123456789abcdef";
	for(unsigned char c:s) {
		switch(c) {
		case '\"':result+="\\\"";break;
		case '\\':result+="\\\\";break;
		case '\n':result+="\\n";break;
		case '\r':result+="\\r";break;
		case '\t':result+="\\t";break;
		default:
			if(c<32){result+="\\u00";result+=digits[c>>4];result+=digits[c&15];}
			else{result+=static_cast<char>(c);}
		}
	}
	return result+"\"";
}
std::string array(const std::vector<std::string>& values) {
	std::string result{"["};bool first=true;
	for(const auto& v:values){if(!first){result+=",";}first=false;result+=v;}
	return result+"]";
}
std::string type(jai::type_info_ptr t){return t?quote(t->canonical_name()):"null";}
std::string boolean(bool value){return value?"true":"false";}
std::string node(const jai::ast_node* value);
template<class T> std::string nodes(const std::vector<T>& values) {
	std::vector<std::string> result;for(const auto& v:values){result.push_back(node(v.get()));}return array(result);
}
std::string parameters(const std::vector<jai::parameter>& values) {
	std::vector<std::string> result;
	for(const auto& p:values){result.push_back("{\"name\":"+quote(p.name)+",\"type\":"+type(p.type)+",\"ref\":"+boolean(p.is_reference)+",\"default\":"+node(p.default_value.get())+"}");}
	return array(result);
}
std::string node(const jai::ast_node* value) {
	if(!value){return "null";}
	std::vector<std::pair<std::string,std::string>> fields;
	auto put=[&](std::string key,std::string v){fields.emplace_back(std::move(key),std::move(v));};
	auto kind=[&](const char* n){put("kind",quote(n));};
	put("line",std::to_string(value->location.line));
#define CASE(name) case jai::node_type::name: { const auto& n=*static_cast<const jai::name*>(value);
#define CHILD(key,field) put(key,node(n.field.get()))
	switch(value->get_type()) {
	CASE(literal_expr)
		kind("literal");auto k=n.value.raw_storage_index();put("tag",std::to_string(k));
		if(k==jai::script_value::TYPEID_NULL){put("value","null");}
		else if(k==jai::script_value::TYPEID_INT){put("value",std::to_string(n.value.unchecked_as_int()));}
		else if(k==jai::script_value::TYPEID_FLOAT){std::ostringstream out;out<<std::setprecision(17)<<n.value.unchecked_as_float();put("value",out.str());}
		else if(k==jai::script_value::TYPEID_BOOL){put("value",boolean(n.value.unchecked_as_bool()));}
		else if(k==jai::script_value::TYPEID_CHAR){put("value",std::to_string(static_cast<unsigned char>(n.value.as_char())));}
		else if(k==jai::script_value::TYPEID_STRING){put("value",quote(n.value.unchecked_as_string()));}
		else{throw std::runtime_error("unsupported literal");}
		break;}
	CASE(identifier_expr)kind("id");put("name",quote(n.name));put("slot",n.slot_index==SIZE_MAX?"null":std::to_string(n.slot_index));break;}
	CASE(binary_expr)kind("binary");put("op",quote(n.op.lexeme));CHILD("left",left);CHILD("right",right);break;}
	CASE(unary_expr)kind("unary");put("op",quote(n.op.lexeme));put("postfix",boolean(n.is_postfix));CHILD("value",operand);break;}
	CASE(assignment_expr)kind("assign");put("op",quote(n.op.lexeme));CHILD("target",target);CHILD("value",value);break;}
	CASE(call_expr)kind("call");CHILD("callee",callee);put("args",nodes(n.arguments));break;}
	CASE(member_expr)kind("member");CHILD("object",object);put("name",quote(n.member));put("static",boolean(n.is_static));break;}
	CASE(ternary_expr)kind("ternary");CHILD("condition",condition);CHILD("then",then_expression);CHILD("else",else_expression);break;}
	CASE(array_literal_expr)kind("array");put("values",nodes(n.elements));break;}
	CASE(map_literal_expr)kind("map");std::vector<std::string> entries;for(const auto& kv:n.entries){entries.push_back("["+node(kv.first.get())+","+node(kv.second.get())+"]");}put("entries",array(entries));break;}
	CASE(this_expr)kind("this");break;}
	CASE(super_expr)kind("super");break;}
	CASE(new_expr)kind("new");put("type",type(n.type));put("args",nodes(n.arguments));break;}
	CASE(yield_expr)kind("yield");CHILD("value",value);break;}
	CASE(throw_expr)kind("throw");CHILD("value",value);break;}
	CASE(cast_expr)kind("cast");put("type",type(n.target));put("ref",boolean(n.by_reference));CHILD("value",operand);break;}
	CASE(expression_stmt)kind("expression");CHILD("value",expression);break;}
	CASE(expression_decl)kind("expression");CHILD("value",expression);break;}
	CASE(statement_decl)kind("statement");CHILD("value",statement);break;}
	CASE(block_stmt)kind("block");put("body",nodes(n.declarations));break;}
	CASE(if_stmt)kind("if");CHILD("condition",condition);CHILD("then",then_statement);CHILD("else",else_statement);break;}
	CASE(while_stmt)kind("while");CHILD("condition",condition);CHILD("body",body);break;}
	CASE(for_stmt)kind("for");CHILD("init",initializer);CHILD("condition",condition);CHILD("update",update);CHILD("body",body);break;}
	CASE(range_for_stmt)kind("range");put("name",quote(n.variable_name));put("type",type(n.element_type));put("ref",boolean(n.is_reference));CHILD("container",container);CHILD("body",body);break;}
	CASE(return_stmt)kind("return");CHILD("value",value);break;}
	CASE(break_stmt)kind("break");break;}
	CASE(continue_stmt)kind("continue");break;}
	CASE(try_stmt)kind("try");put("name",quote(n.catch_var));CHILD("body",try_block);CHILD("catch",catch_block);break;}
	CASE(variable_decl)kind("var");put("name",quote(n.name));put("type",type(n.type));CHILD("value",initializer);break;}
	CASE(function_decl)kind("function");put("name",quote(n.name));put("params",parameters(n.parameters));put("return",type(n.return_type));put("coroutine",boolean(n.is_coroutine));CHILD("body",body);break;}
	CASE(class_decl)kind("class");put("name",quote(n.name));std::vector<std::string> members;for(const auto& m:n.members){members.push_back(node(m.declaration.get()));}put("members",array(members));break;}
	CASE(import_decl)kind("import");put("path",quote(n.path));break;}
	CASE(extern_decl)kind("extern");break;}
	default:throw std::runtime_error("unsupported node "+std::to_string(static_cast<int>(value->get_type()))+" at line "+std::to_string(value->location.line));
	}
#undef CASE
#undef CHILD
	std::string result{"{"};bool first=true;
	for(const auto& [k,v]:fields){if(!first){result+=",";}first=false;result+=quote(k)+":"+v;}
	return result+"}";
}
}
int main(int argc,char** argv) {
	if(argc!=3){std::cerr<<"export_ast INPUT OUTPUT\n";return 2;}
	try{
		auto e=jai::engine::make();jai::stdlib::register_all(*e);
		std::ifstream in(argv[1],std::ios::binary);if(!in){throw std::runtime_error("input unavailable");}
		std::ostringstream text;text<<in.rdbuf();
		auto templates=e->get_registered_template_types();
		jai::lexer lex(text.str(),e->get_symbolizer(),argv[1]);auto tokens=lex.tokenize();
		jai::parser parser(tokens,e->get_symbolizer(),e.get(),templates);auto parsed=parser.parse();
		if(!parsed){throw std::runtime_error(e->format_error(parsed));}
		std::ofstream out(argv[2],std::ios::binary);out<<nodes(parsed.value())<<'\n';
		if(!out){throw std::runtime_error("output failed");}
	}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
