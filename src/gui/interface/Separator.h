#pragma once
#include "Component.h"

namespace ui
{

class Separator : public Component
{
public:
	Kind GetKind() const override { return Kind::Separator; }
	Separator(ui::Point position, ui::Point size) : Component(position, size)
	{ }

	void Draw(const ui::Point& screenPos) override;
};

}