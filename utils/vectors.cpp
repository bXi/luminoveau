#include "vectors.h"

#include "rectangles.h"

template <class T>
v2d_generic<T> v2d_generic<T>::clamp(const rect_generic<T>& target) const
{

    return v2d_generic(
            std::clamp(x, target.x, target.x + target.width),
            std::clamp(y, target.y, target.y + target.height)
            );
}
