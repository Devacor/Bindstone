//Note: BINDSTONE_SERVER is actually a project-wide define, but we put it manually in here for VS Intellisense to work.
#ifndef BINDSTONE_SERVER
#define BINDSTONE_SERVER
#endif
#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Utility/cerealUtility.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "Game/NetworkLayer/gameServer.h"

#include "MV/Utility/taskActions.hpp"

#include <fstream>

#include "glm/mat4x4.hpp"

template<size_t SizeX, size_t SizeY, size_t ResultX>
inline MV::Matrix<SizeX, ResultX> shittyMultiply(const MV::Matrix<SizeX, SizeY>& a_left, const MV::Matrix<SizeX, ResultX>& a_right) {
	MV::Matrix<SizeX, ResultX> result;

	for (size_t x = 0; x < SizeX; ++x) {
		for (size_t common = 0; common < SizeX; ++common) {
			for (size_t y = 0; y < ResultX; ++y) {
				result.access(x, y) += a_left(common, y) * a_right(x, common);
			}
		}
	}

	return result;
}

template<size_t SizeX, size_t SizeY, size_t ResultSize>
inline MV::Matrix<SizeX, ResultSize> newMult(const MV::Matrix<SizeX, SizeY>& a_left, const MV::Matrix<SizeY, ResultSize>& a_right) {
	MV::Matrix<SizeX, ResultSize> result;

	for (size_t x = 0; x < SizeX; ++x) {
		for (size_t y = 0; y < ResultSize; ++y) {
			float sum = 0.0f;
			for (size_t common = 0; common < SizeY; ++common) {
				sum += a_left(common, y) * a_right(x, common);
			}
			result.access(x, y) = sum;
		}
	}

	return result;
}

template<size_t N, size_t M, size_t P>
inline MV::Matrix<P, N> newMult2(const MV::Matrix<M, N>& a_lhs, const MV::Matrix<P, M>& a_rhs) {
	MV::Matrix<P, N> result;

	for (size_t n = 0; n < N; ++n) {
		for (size_t p = 0; p < P; ++p) {
			float sum = 0.0f;
			for (size_t m = 0; m < M; ++m) {
				sum += a_lhs(m, n) * a_rhs(p, m);
			}
			result(p, n) = sum;
		}
	}

	return result;
}

template<size_t N, size_t M, size_t P>
inline MV::Matrix<P, N> newMult2_perftest(const MV::Matrix<M, N>& a_lhs, const MV::Matrix<P, M>& a_rhs) {
	MV::Matrix<P, N> result;

	for (size_t n = 0; n < N; ++n) {
		for (size_t p = 0; p < P; ++p) {
			for (size_t m = 0; m < M; ++m) {
				result(p, n) += a_lhs(m, n) * a_rhs(p, m);
			}
		}
	}

	return result;
}


inline MV::Point<> newMult2(const MV::Matrix<4, 4>& a_lhs, const MV::Point<>& a_rhs) {
	static const size_t M = 4;
	static const size_t N = 4;
	static const size_t P = 1;
	MV::Point<> result;
	for (size_t n = 0; n < N; ++n) {
		float sum = 0.0f;
		for (size_t m = 0; m < M; ++m) {
			sum += a_lhs(m, n) * a_rhs(m);
		}
		result(n) = sum;
	}

	return result;
}

template<size_t N, size_t M, size_t P>
MV::Matrix<4,4> multiplyTestTwoA(MV::Matrix<4, 4>& a_lhs, MV::Matrix<4, 4>& a_rhs) {
	MV::Matrix<P, N> result;
	for (size_t n = 0; n < N; ++n) {
		for (size_t p = 0; p < P; ++p) {
			auto& resultLoc = result(p, n);
			for (size_t m = 0; m < M; ++m) {
				resultLoc += a_lhs(m, n) * a_rhs(p, m);
			}
		}
	}
	return result;
}

template<size_t N, size_t M, size_t P>
MV::Matrix<4, 4> multiplyTestTwoB(MV::Matrix<4, 4>& a_lhs, MV::Matrix<4, 4>& a_rhs) {
	MV::Matrix<P, N> result;
	for (size_t n = 0; n < N; ++n) {
		for (size_t p = 0; p < P; ++p) {
			for (size_t m = 0; m < M; ++m) {
				result(p, n) += a_lhs(m, n) * a_rhs(p, m);
			}
		}
	}
	return result;
}

static int ITERATIONS = 2;

void multiplyTest(std::string name, MV::Matrix<4, 4> &a, MV::Matrix<4, 4> & b) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < ITERATIONS; ++i) {
		a = newMult2(a, b);
	}
	auto elapsed = timer.stop();
	std::cout << "Normal Test [" << name << "] == " << elapsed << "s\n";
}

void multiplyTestShit(std::string name, MV::Matrix<4, 4> & a, MV::Matrix<4, 4> & b) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < ITERATIONS; ++i) {
		a = newMult2_perftest(a, b);
	}
	auto elapsed = timer.stop();
	std::cout << "Shit Test [" << name << "] == " << elapsed << "s\n";
}

void multiplyTestGlm(std::string name, glm::mat4x4& a, glm::mat4x4& b) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < 1000000; ++i) {
		a = a * b;
	}
	auto elapsed = timer.stop();
	std::cout << "GLM Test [" << name << "] == " << elapsed << "s\n";
}

template <typename T1, typename T2, typename F>
auto performanceTest(std::string name, T1& a, T2& b, const F &f) -> decltype(f(a, b)) {
	MV::Stopwatch timer;
	timer.start();
	decltype(f(a, b)) result;
	for (int i = 0; i < 1000000; ++i) {
		result += f(a, b);
	}
	auto elapsed = timer.stop();
	std::cout << "[" << name << "] == " << elapsed << "s\n";
	return result;
}

auto performanceTestInvert(std::string name, glm::mat4x4& a) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < 1000000; ++i) {
		a = glm::inverse(a);
	}
	auto elapsed = timer.stop();
	std::cout << "[" << name << "] == " << elapsed << "s\n";
}

auto performanceTestInvert(std::string name, MV::TransformMatrix& a) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < 1000000; ++i) {
		a = MV::inverse(a);
	}
	auto elapsed = timer.stop();
	std::cout << "[" << name << "] == " << elapsed << "s\n";
}

// void multiplyTest(std::string name, boost::numeric::ublas::matrix<float>& a, boost::numeric::ublas::matrix<float>& b) {
// 	MV::Stopwatch timer;
// 	timer.start();
// 	for (int i = 0; i < 1000000; ++i) {
// 		boost::numeric::ublas::axpy_prod(a, b, a, true);
// 	}
// 	auto elapsed = timer.stop();
// 	std::cout << "Test [" << name << "] == " << elapsed << "s\n";
// }


std::ostream& operator<<(std::ostream& os, const glm::mat4x4& a_matrix) {
	for (int y = 0; y < 4; ++y) {
		os << "[";
		for (int x = 0; x < 4; ++x) {
			os << a_matrix[x][y] << (x != 3 ? ", " : "]");
		}
		os << "\n";
	}
	os << std::endl;
	return os;
}

int main(int, char *[]) {
	Managers managers({"", ""});
	managers.timer.start();
	/*
	MV::TransformMatrix a1, b1, a2, b2, a3, b3;
	glm::mat4x4 ga1, gb1, ga2, gb2;

	for (int x = 0; x < 4; ++x) {
		for (int y = 0; y < 4; ++y) {
			ga1[x][y] = ga2[x][y] = a1(x, y) = a2(x, y) = a3(x, y) = MV::randomNumber(0.2f, 5.0f);
			gb1[x][y] = gb2[x][y] = b1(x, y) = b2(x, y) = b3(x, y) = MV::randomNumber(0.2f, 5.0f);
		}
	}

	std::cout << "BEFORE: GA1" << std::endl << ga1 << std::endl << "______________" << std::endl;
	
	multiplyTestGlm("glm", ga1, gb1);
	performanceTest("multiplyTestTwoA", a2, b2, multiplyTestTwoA<4, 4, 4>);
	performanceTest("multiplyTestTwoB", a2, b2, multiplyTestTwoB<4, 4, 4>);
	performanceTest("multiplyTestThree", a2, b2, MV::operator*<4, 4, 4>);
	//performanceTest("Unrolled", a1, b1, MV::unrolledMultiply);
	

	auto invertedGLM = glm::inverse(ga1);
	auto invertedMV = a1.inverse();

	performanceTestInvert("MV", invertedMV);
	performanceTestInvert("GLM", invertedGLM);

	std::cout << "Inverted GLM" << std::endl << invertedGLM << std::endl << "_____" << std::endl;
	std::cout << "Inverted MV" << std::endl << invertedMV << std::endl << "_____" << std::endl;

	std::cout << "Basic A1" << std::endl << a2 << std::endl << "______________" << std::endl;
	std::cout << "StaticFor A2" << std::endl << a1 << std::endl << "______________" << std::endl;
	std::cout << "GA1" << std::endl << ga1 << std::endl << "______________" << std::endl;

	MV::Point<> locP(1.0f, 2.0f, 3.0f);
	MV::Matrix<1, 4> locM;
	locM(0, 0) = 1.0f;
	locM(0, 1) = 2.0f;
	locM(0, 2) = 3.0f;
	locM(0, 3) = 1.0f;

	auto multiplyResult2 = a1 * locP;
	auto multiplyResult3 = a1 * locM;

	//MV::info("Result1:\n", multiplyResult);
	MV::info("Result2:\n", multiplyResult2);
	MV::info("Result2:\n", multiplyResult3);
	system("PAUSE");
	return 0;
	*/

	MV::initializeSpineBindings();

	bool done = false;
	auto server = std::make_shared<GameServer>(managers);
	MV::Task statDisplay;
	statDisplay.also("PrintBandwidth", [&](MV::Task&, double) {
		if (server->server()) {
			auto sent = static_cast<double>(server->server()->bytesPerSecondSent()) / 1024.0;
			auto received = static_cast<double>(server->server()->bytesPerSecondReceived()) / 1024.0;
			if (sent > 0 || received > 0) {
				MV::info("Server Sent: [", sent, "]kbs Received: [", received, "]kbs");
			}
		}
		return true;
	}).recent()->localInterval(1.0);
	while (!done) {
		managers.pool.run();
		auto tick = managers.timer.delta("tick");
		server->update(tick);
		statDisplay.update(tick);
		std::this_thread::yield();
	}

	return 0;
}