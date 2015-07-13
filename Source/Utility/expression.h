#ifndef _MV_EXPRESSION_H_
#define _MV_EXPRESSION_H_

#include "Render/points.h"
#include "require.hpp"
#include <map>
#include <memory>

namespace MV {
	struct FunctionCaller;
	struct ExpressionDetail;

	class Expression {
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision()> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision)> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision)> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f);
		static FunctionCaller makeFunctionCaller(const std::string &a_name, std::function<MV::PointPrecision(MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision, MV::PointPrecision)> a_f);
	public:
		Expression(const std::string &a_expressionString, const std::map<std::string, MV::PointPrecision> &a_values = std::map<std::string, MV::PointPrecision>());
		~Expression();

		MV::PointPrecision operator()() {
			return evaluate();
		}

		MV::PointPrecision evaluate(const std::string &a_newFormula = "");

		MV::PointPrecision& operator[](const std::string &a_key){
			return number(a_key);
		}

		MV::PointPrecision& number(const std::string &a_key);

		Expression& number(const std::string &a_key, PointPrecision a_value);

		template <typename T>
		Expression& function(const std::string &a_key, T f);

	private:
		std::string expressionString;
		std::map<std::string, PointPrecision> values;
		std::map<std::string, FunctionCaller> functions;
		
		bool validExpression = false;
		bool dirty = true;

		ExpressionDetail* detail;
	};
}
#endif