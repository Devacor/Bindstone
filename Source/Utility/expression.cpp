#include "expression.h"
#pragma warning(push, 0) 
#include "Exprtk/exprtk.hpp"
#pragma warning(pop) 

namespace MV {
	struct ExpressionDetail {
		typedef exprtk::symbol_table<MV::PointPrecision> symbol_table_t;
		typedef exprtk::expression<MV::PointPrecision> expression_t;
		typedef exprtk::parser<MV::PointPrecision> parser_t;

		symbol_table_t symbolTable;
		expression_t expression;
		parser_t parser;
	};

	struct FunctionCaller : exprtk::ifunction<PointPrecision> {
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision()> a_f) :
			name(a_name),
			f0(a_f),
			exprtk::ifunction<PointPrecision>(0) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision)> a_f) :
			name(a_name),
			f1(a_f),
			exprtk::ifunction<PointPrecision>(1) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision)> a_f) :
			name(a_name),
			f2(a_f),
			exprtk::ifunction<PointPrecision>(2) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) :
			name(a_name),
			f3(a_f),
			exprtk::ifunction<PointPrecision>(3) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) :
			name(a_name),
			f4(a_f),
			exprtk::ifunction<PointPrecision>(4) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) :
			name(a_name),
			f5(a_f),
			exprtk::ifunction<PointPrecision>(5) {
		}
		FunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) :
			name(a_name),
			f6(a_f),
			exprtk::ifunction<PointPrecision>(6) {
		}

		inline virtual MV::PointPrecision operator()()
		{
			require<RangeException>(f0, "Expression function call: [", name, "]: Wrong number of arguments. 0 arguments supplied!");
			return f0();
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1)
		{
			require<RangeException>(f1, "Expression function call: [", name, "]: Wrong number of arguments. 1 argument supplied!");
			return f1(a_1);
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1, const PointPrecision& a_2)
		{
			require<RangeException>(f2, "Expression function call: [", name, "]: Wrong number of arguments. 2 arguments supplied!");
			return f2(a_1, a_2);
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1, const PointPrecision& a_2, const PointPrecision& a_3)
		{
			require<RangeException>(f3, "Expression function call: [", name, "]: Wrong number of arguments. 3 arguments supplied!");
			return f3(a_1, a_2, a_3);
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1, const PointPrecision& a_2, const PointPrecision& a_3, const PointPrecision& a_4)
		{
			require<RangeException>(f4, "Expression function call: [", name, "]: Wrong number of arguments. 4 arguments supplied!");
			return f4(a_1, a_2, a_3, a_4);
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1, const PointPrecision& a_2, const PointPrecision& a_3, const PointPrecision& a_4, const PointPrecision& a_5)
		{
			require<RangeException>(f5, "Expression function call: [", name, "]: Wrong number of arguments. 5 arguments supplied!");
			return f5(a_1, a_2, a_3, a_4, a_5);
		}

		inline virtual MV::PointPrecision operator()(const PointPrecision& a_1, const PointPrecision& a_2, const PointPrecision& a_3, const PointPrecision& a_4, const PointPrecision& a_5, const PointPrecision &a_6)
		{
			require<RangeException>(f6, "Expression function call: [", name, "]: Wrong number of arguments. 6 arguments supplied!");
			return f6(a_1, a_2, a_3, a_4, a_5, a_6);
		}
		std::string name;
		std::function<MV::PointPrecision()> f0;
		std::function<MV::PointPrecision(MV::PointPrecision)> f1;
		std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision)> f2;
		std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> f3;
		std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> f4;
		std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> f5;
		std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> f6;
	};

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision()> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	MV::FunctionCaller Expression::makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f) {
		return FunctionCaller(a_name, a_f);
	}

	Expression::Expression(const std::string &a_expressionString, const std::map<std::string, MV::PointPrecision> &a_values /*= std::map<std::string, MV::PointPrecision>()*/) :
		detail(new ExpressionDetail()),
		expressionString(a_expressionString),
		values(a_values) {

		for (auto&& v : values) {
			detail->symbolTable.add_variable(v.first, v.second);
		}
		detail->expression.register_symbol_table(detail->symbolTable);
	}

	Expression::~Expression() {
		delete detail;
	}

	MV::PointPrecision Expression::evaluate(const std::string &a_newFormula /*= ""*/) {
		if (!a_newFormula.empty()) {
			expressionString = a_newFormula;
			dirty = true;
		}
		if (dirty) {
			validExpression = false;
			if (detail->parser.compile(expressionString, detail->expression)) {
				validExpression = true;
			}
		}
		MV::require<MV::LogicException>(validExpression, "Invalid Expression: ", expressionString, "\n", (detail->parser.error_count() > 0 ? detail->parser.error() : ""));
		return detail->expression.value();
	}

	MV::PointPrecision& Expression::number(const std::string &a_key) {
		auto val = values.find(a_key);
		if (val != values.end()) {
			return val->second;
		}
		dirty = true;
		auto& result = values[a_key];
		detail->symbolTable.add_variable(a_key, result);
		return result;
	}

	Expression& Expression::number(const std::string &a_key, PointPrecision a_value) {
		auto val = values.find(a_key);
		if (val != values.end()) {
			val->second = a_value;
		}
		dirty = true;
		auto& result = values[a_key];
		detail->symbolTable.add_variable(a_key, result);
		return *this;
	}

	template <typename T>
	Expression& Expression::function(const std::string &a_key, T f) {
		auto func = functions.find(a_key);
		if (func != functions.end()) {
			func->second = makeFunctionCaller(a_key, f);
		}
		dirty = true;
		auto result = functions.emplace(std::make_pair(a_key, makeFunctionCaller(a_key, f)));
		require<ResourceException>(result.second, "Failed to add function to expression, probably already exists: [", a_key, "] -> ", expressionString);
		detail->symbolTable.add_function(a_key, result.first->second);
		return *this;
	}

	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision()>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision)>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision)>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)>);
	template Expression& Expression::function(const std::string &, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)>);

}