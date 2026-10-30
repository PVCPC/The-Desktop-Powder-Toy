#pragma once
#include "gui/interface/Button.h"

class MenuButton : public ui::Button
{
public:
	Kind GetKind() const override { return Kind::Other; }
	using ui::Button::Button;
	int menuID;
	bool needsClick;
};
