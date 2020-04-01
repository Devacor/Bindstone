#include "scroller.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include "text.h"

CEREAL_REGISTER_TYPE(MV::Scene::Scroller);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenescroller);

namespace MV {
	namespace Scene {

		Scroller::Scroller(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse) :
			Clickable(a_owner, a_mouse) {
			stopEatingTouches();
			
			appendClickPriority = 1000;

			onDrag.connect("_INTERNAL_SCROLL", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) mutable {
				if(contentView){
					if (!isDragging && a_clickable->dragTime() <= cancelTimeThreshold) {
						if (a_clickable->totalDragDistance() > dragStartThreshold) {
							isDragging = true;
							auto buttons = contentView->componentsInChildren<MV::Scene::Clickable>(false, false);
							MV::visit(buttons, [&](const MV::Scene::SafeComponent<MV::Scene::Clickable> &a_button) {
								a_button->cancelPress();
							});
						} else if (a_clickable->dragTime() > cancelTimeThreshold) {
							cancelPress();
							return;
						}
					}
					if (isDragging) {
						shiftContentByDelta(deltaPosition);
					}
				}
			});
		}

		/*
		template <class T>
		std::vector <std::vector<T>> Multiply(std::vector <std::vector<T>>& a, std::vector <std::vector<T>>& b)
		{
			const int n = a.size();     // a rows
			const int m = a[0].size();  // a cols
			const int p = b[0].size();  // b cols

			std::vector <std::vector<T>> c(n, std::vector<T>(p, 0));
			for (auto j = 0; j < p; ++j)
			{
				for (auto k = 0; k < m; ++k)
				{
					for (auto i = 0; i < n; ++i)
					{
						c[i][j] += a[i][k] * b[k][j];
					}
				}
			}
			return c;
		}*/
		/*
		Matrix operator*(const Matrix& other) const {

			assert(cols() == other.rows());

			Matrix out(rows(), other.cols());

			for (std::size_t i = 0; i < rows(); ++i) {
				for (std::size_t j = 0; j < other.cols(); ++j) {
					double sum{ 0 };
					for (std::size_t k = 0; k < cols(); ++k) {
						sum += (*this)(i, k) * other(k, j);
					}
					out(i, j) = sum;
				}
			}
			return out;
		}*/

		template<size_t N, size_t M, size_t K>
		Matrix<N, K> MULT(const Matrix<N, M>& a_lhs, const Matrix<M, K>& a_rhs) {
			Matrix<N, K> result;
			for (size_t n = 0; n != N; n++) {
				for (size_t k = 0; k != K; k++) {
					for (size_t m = 0; m != M; m++) {
						result(n, k) += a_lhs(n, m) * a_rhs(m, k);
					}
				}
			}
			return result;
		}

		void Scroller::shiftContentByDelta(const MV::Point<int> & deltaPosition) {
			if (deltaPosition.x == 0 && deltaPosition.y == 0) {
				return;
			}
			auto self = owner();
			
			auto worldDelta = self->renderer().worldFromScreen(deltaPosition) - self->renderer().worldFromScreen(MV::Point<int>());
			auto contentBounds = contentView->worldBounds();
			auto ourBounds = worldBounds();

			if (!horizontalAllowed || contentBounds.width() >= ourBounds.width()) {
				worldDelta.x = 0;
			}
			if (!verticalAllowed || contentBounds.height() >= ourBounds.height()) {
				worldDelta.y = 0;
			}

			contentBounds += worldDelta;

			if (worldDelta.x != 0) {
				if (contentBounds.minPoint.x < ourBounds.minPoint.x) {
					worldDelta.x += ourBounds.minPoint.x - contentBounds.minPoint.x;
				} else if (contentBounds.maxPoint.x > ourBounds.maxPoint.x) {
					worldDelta.x -= contentBounds.maxPoint.x - ourBounds.maxPoint.x;
				}
			}
			if (worldDelta.y != 0) {
				if (contentBounds.minPoint.y < ourBounds.minPoint.y) {
					worldDelta.y += ourBounds.minPoint.y - contentBounds.minPoint.y;
				}
				else if (contentBounds.maxPoint.y > ourBounds.maxPoint.y) {
					worldDelta.y -= contentBounds.maxPoint.y - ourBounds.maxPoint.y;
				}
			}
			contentView->translate(self->localFromWorld(self->worldPosition() + worldDelta));
		}

		std::shared_ptr<Scroller> Scroller::content(const std::shared_ptr<Node> &a_content) {
			contentView = a_content;
			shiftContentByDelta({0, 0});
			return std::static_pointer_cast<Scroller>(shared_from_this());
		}


		std::shared_ptr<Component> Scroller::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Clickable::cloneHelper(a_clone);
			auto scrollerClone = std::static_pointer_cast<Scroller>(a_clone);
			if (contentView) {
				auto foundHandle = scrollerClone->owner()->get(contentView->id());
				scrollerClone->content(foundHandle);
			}
			return a_clone;
		}

		void Scroller::updateImplementation(double a_delta) {
			Clickable::updateImplementation(a_delta);
// 			if (contentView && outOfBounds) {
// 				auto localContentPosition = owner()->localFromWorld(contentView->worldPosition());
// 				auto localContentBounds = owner()->localFromWorld(contentView->worldBounds());
// 				auto ourBounds = bounds();
// 
// 				if (localContentBounds.minPoint.x < ourBounds.minPoint.x) {
// 					localContentPosition.x += (ourBounds.minPoint.x - localContentBounds.minPoint.x);
// 				}
// 				if (localContentBounds.minPoint.y > ourBounds.minPoint.y) {
// 					localContentPosition.y -= ourBounds.minPoint
// 				}
// 			}
		}

	}
}
