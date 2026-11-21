// The looks on the simulator's digit keys (also accepted by --shot): moods are held, reactions play once.
#pragma once
#include "body/looks.h"

namespace nestbox {

constexpr Look DIGIT_KEY_LOOKS[10] = {Look::Sleep, Look::Wander, Look::Idle, Look::Type,  Look::TypeFast,
                                        Look::Happy,  Look::Alert,  Look::Sad, Look::Dizzy, Look::Wake};

}  // namespace nestbox
