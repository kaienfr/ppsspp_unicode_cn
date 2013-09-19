// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "GamepadEmu.h"
#include "base/colorutil.h"
#include "math/math_util.h"
#include "ui/virtual_input.h"
#include "ui/ui_context.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "ui_atlas.h"
#include "Core/HLE/sceCtrl.h"

#include <algorithm>

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
#define USE_PAUSE_BUTTON 1
#else
#define USE_PAUSE_BUTTON 0
#endif

void MultiTouchButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[bgImg_];
	w = image.w * scale_;
	h = image.h * scale_;
}

void MultiTouchButton::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && bounds_.Contains(input.x, input.y)) {
		pointerDownMask_ |= 1 << input.id;
	}
	if (input.flags & TOUCH_MOVE) {
		if (bounds_.Contains(input.x, input.y))
			pointerDownMask_ |= 1 << input.id;
		else
			pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_UP) {
		pointerDownMask_ &= ~(1 << input.id);
	}
}

void MultiTouchButton::Draw(UIContext &dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;
	
	float scale = scale_;
	if (IsDown()) {
		scale *= 2.0f;
		opacity *= 1.15f;
	}
	uint32_t colorBg = colorAlpha(0xc0b080, opacity);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	dc.Draw()->DrawImageRotated(bgImg_, bounds_.centerX(), bounds_.centerY(), scale, angle_ * (M_PI * 2 / 360.0f), colorBg, flipImageH_);
	dc.Draw()->DrawImageRotated(img_, bounds_.centerX(), bounds_.centerY(), scale, angle_ * (M_PI * 2 / 360.0f), color);
}

void BoolButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (down != lastDown) {
		*value_ = down;
	}
}

void PSPButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		__CtrlButtonDown(pspButtonBit_);
	} else if (lastDown && !down) {
		__CtrlButtonUp(pspButtonBit_);
	}
}

bool PSPButton::IsDown() {
	return (__CtrlPeekButtons() & pspButtonBit_) != 0;
}


PSPCross::PSPCross(int arrowIndex, int overlayIndex, float scale, float radius, UI::LayoutParams *layoutParams)
	: UI::View(layoutParams), arrowIndex_(arrowIndex), overlayIndex_(overlayIndex), scale_(scale), radius_(radius), dragPointerId_(-1), down_(0) {
}

void PSPCross::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = radius_ * 4;
	h = radius_ * 4;
}

void PSPCross::Touch(const TouchInput &input) {
	int lastDown = down_;

	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPCross::ProcessTouch(float x, float y, bool down) {
	float stick_size_ = radius_;
	float inv_stick_size = 1.0f / (stick_size_ * scale_);
	const float deadzone = 0.17f;

	float dx = (x - bounds_.centerX()) * inv_stick_size;
	float dy = (y - bounds_.centerY()) * inv_stick_size;
	float rad = sqrtf(dx*dx+dy*dy);
	if (rad < deadzone || rad > 2.0f)
		down = false;

	int ctrlMask = 0;
	int lastDown = down_;
	if (down) {
		int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 8) + 0.5f)) & 7;
		switch (direction) {
		case 0: ctrlMask |= CTRL_RIGHT; break;
		case 1: ctrlMask |= CTRL_RIGHT | CTRL_DOWN; break;
		case 2: ctrlMask |= CTRL_DOWN; break;
		case 3: ctrlMask |= CTRL_DOWN | CTRL_LEFT; break;
		case 4: ctrlMask |= CTRL_LEFT; break;
		case 5: ctrlMask |= CTRL_UP | CTRL_LEFT; break;
		case 6: ctrlMask |= CTRL_UP; break;
		case 7: ctrlMask |= CTRL_UP | CTRL_RIGHT; break;
		}
	}

	down_ = ctrlMask;
	int pressed = down_ & ~lastDown;
	int released = (~down_) & lastDown;
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	for (int i = 0; i < 4; i++) {
		if (pressed & dir[i]) __CtrlButtonDown(dir[i]);
		if (released & dir[i]) __CtrlButtonUp(dir[i]);
	}
}

void PSPCross::Draw(UIContext &dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;

	uint32_t colorBg = colorAlpha(0xc0b080, opacity);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	static const float xoff[4] = {1, 0, -1, 0};
	static const float yoff[4] = {0, 1, 0, -1};
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	int buttons = __CtrlPeekButtons();
	for (int i = 0; i < 4; i++) {
		float x = bounds_.centerX() + xoff[i] * radius_;
		float y = bounds_.centerY() + yoff[i] * radius_;
		float angle = i * M_PI / 2;
		float imgScale = (buttons & dir[i]) ? scale_ * 2 : scale_;
		dc.Draw()->DrawImageRotated(arrowIndex_, x, y, imgScale, angle + PI, colorBg, false);
		if (overlayIndex_ != -1)
			dc.Draw()->DrawImageRotated(overlayIndex_, x, y, imgScale, angle + PI, color);
	}
}

PSPStick::PSPStick(int bgImg, int stickImg, int stick, float scale, UI::LayoutParams *layoutParams) 
	: UI::View(layoutParams), dragPointerId_(-1), bgImg_(bgImg), stickImageIndex_(stickImg), stick_(stick), scale_(scale) {
	stick_size_ = 50;
}

void PSPStick::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[bgImg_];
	w = image.w;
	h = image.h;
}

void PSPStick::Draw(UIContext &dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;

	uint32_t colorBg = colorAlpha(0xc0b080, opacity);
	uint32_t color = colorAlpha(0x808080, opacity);

	float stickX = bounds_.centerX();
	float stickY = bounds_.centerY();

	float dx, dy;
	__CtrlPeekAnalog(stick_, &dx, &dy);

	dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, colorBg, ALIGN_CENTER);
}

void PSPStick::Touch(const TouchInput &input) {
	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPStick::ProcessTouch(float x, float y, bool down) {
	if (down) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - bounds_.centerX()) * inv_stick_size;
		float dy = (y - bounds_.centerY()) * inv_stick_size;
		// Do not clamp to a circle! The PSP has nearly square range!

		// Old code to clamp to a circle
		// float len = sqrtf(dx * dx + dy * dy);
		// if (len > 1.0f) {
		//	dx /= len;
		//	dy /= len;
		//}

		// Still need to clamp to a square
		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		__CtrlSetAnalogX(dx, stick_);
		__CtrlSetAnalogY(-dy, stick_);
	} else {
		__CtrlSetAnalogX(0.0f, stick_);
		__CtrlSetAnalogY(0.0f, stick_);
	}
}



UI::ViewGroup *CreatePadLayout(bool *pause) {
	using namespace UI;

	AnchorLayout *root = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	// TODO: See if we can make some kind of global scaling for views instead of this hackery.
	float scale = g_Config.fButtonScale;

	// const int button_spacing = 50 * controlScale;
	const int button_spacing = 50 * scale;
	const int arrow_spacing = 40 * scale;

	const int circleX = 40 * scale;
	const int circleY = 120 * scale;

	const int startX = 170 * scale;
	const int leftX = 40 * scale;
	const int leftY = (g_Config.bShowAnalogStick ? 250 : 120) * scale;
	const int stickX = leftX + arrow_spacing;
	const int stickY = 80 * scale;
	const int bottomStride = 100 * scale;

	const int crosspadRadius = 40 * scale;

	const int halfW = dp_xres / 2;

#if USE_PAUSE_BUTTON
	root->Add(new BoolButton(pause, I_ROUND, I_ARROW, scale, new AnchorLayoutParams(halfW, 20, NONE, NONE, true)))->SetAngle(90);
#endif

	if (g_Config.bShowTouchControls) {
		root->Add(new BoolButton(&PSP_CoreParameter().unthrottle, I_RECT, I_ARROW, scale, new AnchorLayoutParams(NONE, NONE, startX + 2 * bottomStride, 30, true)))->SetAngle(180);

		root->Add(new PSPButton(CTRL_CIRCLE, I_ROUND, I_CIRCLE, scale, new AnchorLayoutParams(NONE, NONE, circleX, circleY, true)));
		root->Add(new PSPButton(CTRL_CROSS, I_ROUND, I_CROSS, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing, circleY - button_spacing, true)));
		root->Add(new PSPButton(CTRL_TRIANGLE, I_ROUND, I_TRIANGLE, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing, circleY + button_spacing, true)));
		root->Add(new PSPButton(CTRL_SQUARE, I_ROUND, I_SQUARE, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing * 2, circleY, true)));

		root->Add(new PSPButton(CTRL_START, I_RECT, I_START, scale, new AnchorLayoutParams(NONE, NONE, startX, 30, true)));
		root->Add(new PSPButton(CTRL_SELECT, I_RECT, I_SELECT, scale, new AnchorLayoutParams(NONE, NONE, startX + bottomStride, 30, true)));

		root->Add(new PSPButton(CTRL_LTRIGGER, I_SHOULDER, I_L, scale, new AnchorLayoutParams(10, 10, NONE, NONE, false)));
		root->Add(new PSPButton(CTRL_RTRIGGER, I_SHOULDER, I_R, scale, new AnchorLayoutParams(NONE, 10, 10, NONE, false)))->FlipImageH(true);

		root->Add(new PSPCross(I_DIR, I_ARROW, scale, crosspadRadius, new AnchorLayoutParams(leftX + arrow_spacing, NONE, NONE, leftY, true)));

		if (g_Config.bShowAnalogStick) {
			root->Add(new PSPStick(I_STICKBG, I_STICK, 0, scale, new AnchorLayoutParams(stickX, NONE, NONE, stickY, true)));
		}
	}

	return root;
}
