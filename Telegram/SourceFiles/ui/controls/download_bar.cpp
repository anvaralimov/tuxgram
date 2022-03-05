/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/download_bar.h"

#include "ui/widgets/buttons.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

constexpr auto kFullArcLength = 360 * 16;

} // namespace

DownloadBar::DownloadBar(
	not_null<QWidget*> parent,
	rpl::producer<DownloadBarProgress> progress)
: _button(
	parent,
	object_ptr<RippleButton>(parent, st::dialogsMenuToggle.ripple))
, _shadow(parent)
, _progress(std::move(progress))
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	_button.hide(anim::type::instant);
	_shadow.showOn(_button.shownValue());
	_button.setDirectionUp(false);
	_button.entity()->resize(0, st::downloadBarHeight);
	_button.entity()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(_button.entity());
		paint(p, clip);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshIcon();
	}, lifetime());
	refreshIcon();

	_progress.value(
	) | rpl::start_with_next([=](const DownloadBarProgress &progress) {
		refreshInfo(progress);
	}, lifetime());
}

DownloadBar::~DownloadBar() = default;

void DownloadBar::show(DownloadBarContent &&content) {
	_button.toggle(content.count > 0, anim::type::normal);
	if (!content.count) {
		return;
	}
	if (!_radial.animating()) {
		_radial.start(computeProgress());
	}
	_content = content;
	_title.setMarkedText(
		st::defaultTextStyle,
		(content.count > 1
			? Ui::Text::Bold(
				tr::lng_profile_files(tr::now, lt_count, content.count))
			: content.singleName));
	refreshInfo(_progress.current());
}

void DownloadBar::refreshIcon() {
	_documentIconOriginal = st::downloadIconDocument.instance(
		st::windowFgActive->c,
		style::kScaleMax / style::DevicePixelRatio());
	const auto make = [&](int size) {
		auto result = _documentIconOriginal.scaledToWidth(
			size * style::DevicePixelRatio(),
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		return result;
	};
	_documentIcon = make(st::downloadIconSize);
	_documentIconDone = make(st::downloadIconSizeDone);
}

void DownloadBar::refreshInfo(const DownloadBarProgress &progress) {
	_info.setMarkedText(
		st::downloadInfoStyle,
		(progress.ready < progress.total
			? Text::WithEntities(
				FormatDownloadText(progress.ready, progress.total))
			: Text::Link((_content.count > 1)
				? tr::lng_downloads_view_in_section(tr::now)
				: tr::lng_downloads_view_in_chat(tr::now))));
	_button.entity()->update();
}

bool DownloadBar::isHidden() const {
	return _button.isHidden();
}

int DownloadBar::height() const {
	return _button.height();
}

rpl::producer<int> DownloadBar::heightValue() const {
	return _button.heightValue();
}

rpl::producer<bool> DownloadBar::shownValue() const {
	return _button.shownValue();
}

void DownloadBar::setGeometry(int left, int top, int width, int height) {
	_button.resizeToWidth(width);
	_button.moveToLeft(left, top);
	_shadow.setGeometry(left, top - st::lineWidth, width, st::lineWidth);
}

rpl::producer<> DownloadBar::clicks() const {
	return _button.entity()->clicks() | rpl::to_empty;
}

rpl::lifetime &DownloadBar::lifetime() {
	return _button.lifetime();
}

void DownloadBar::paint(Painter &p, QRect clip) {
	const auto button = _button.entity();
	const auto outerw = button->width();
	const auto over = button->isOver() || button->isDown();
	const auto &icon = over ? st::downloadArrowOver : st::downloadArrow;
	p.fillRect(clip, st::windowBg);
	button->paintRipple(p, 0, 0);

	const auto size = st::downloadLoadingSize;
	const auto added = 3 * st::downloadLoadingLine;
	const auto skipx = st::downloadLoadingLeft;
	const auto skipy = (button->height() - size) / 2;
	const auto full = QRect(
		skipx - added,
		skipy - added,
		size + added * 2,
		size + added * 2);
	if (full.intersects(clip)) {
		const auto loading = _radial.computeState();
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawEllipse(full.marginsRemoved({ added, added, added, added }));

			if (loading.shown > 0) {
				p.setOpacity(loading.shown);
				auto pen = st::windowBgActive->p;
				pen.setWidth(st::downloadLoadingLine);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				const auto m = added / 2.;
				auto rect = QRectF(full).marginsRemoved({ m, m, m, m });
				if (loading.arcLength < kFullArcLength) {
					p.drawArc(rect, loading.arcFrom, loading.arcLength);
				} else {
					p.drawEllipse(rect);
				}
				p.setOpacity(1.);
			}
		}
		p.drawImage(
			full.x() + (full.width() - st::downloadIconSize) / 2,
			full.y() + (full.height() - st::downloadIconSize) / 2,
			_documentIcon);
	}

	const auto minleft = std::min(
		st::downloadTitleLeft,
		st::downloadInfoLeft);
	const auto maxwidth = outerw - minleft;
	if (!clip.intersects({ minleft, 0, maxwidth, st::downloadBarHeight })) {
		return;
	}
	const auto right = st::downloadArrowRight + icon.width();
	const auto available = button->width() - st::downloadTitleLeft - right;
	p.setPen(st::windowBoldFg);
	_title.drawLeftElided(
		p,
		st::downloadTitleLeft,
		st::downloadTitleTop,
		available,
		outerw);

	p.setPen(st::windowSubTextFg);
	p.setTextPalette(st::defaultTextPalette);
	_info.drawLeftElided(
		p,
		st::downloadInfoLeft,
		st::downloadInfoTop,
		available,
		outerw);

	const auto iconTop = (st::downloadBarHeight - icon.height()) / 2;
	icon.paint(p, outerw - right, iconTop, outerw);
}

float64 DownloadBar::computeProgress() const {
	const auto now  = _progress.current();
	return now.total ? (now.ready / float64(now.total)) : 0.;
}

void DownloadBar::radialAnimationCallback(crl::time now) {
	const auto finished = (_content.done == _content.count);
	const auto updated = _radial.update(computeProgress(), finished, now);
	if (!anim::Disabled() || updated) {
		const auto button = _button.entity();
		const auto size = st::downloadLoadingSize;
		const auto added = 3 * st::downloadLoadingLine;
		const auto skipx = st::downloadLoadingLeft;
		const auto skipy = (button->height() - size) / 2;
		const auto full = QRect(
			skipx - added,
			skipy - added,
			size + added * 2,
			size + added * 2);
		button->update(full);
	}
}

} // namespace Ui
