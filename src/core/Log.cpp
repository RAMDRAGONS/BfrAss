#include "core/Log.hpp"

namespace bfrass {

const Log& Log::null() {
    static const Log instance;
    return instance;
}

} // namespace bfrass
