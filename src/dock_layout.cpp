//----------------------------------------------------------------------------------
//	自由分割用レイアウトの生成
//----------------------------------------------------------------------------------
#include "dock_layout.h"
#include "common.h"

#include <cstdlib>

namespace tpgd {

// 仮位置の間隔 (診断で位置の誤差は 0 と確認済み。十分に離して取り違えを防ぐ)
constexpr int SLOT_STEP = 40;
constexpr int SLOTS_PER_ROW = 16;
// 照合で許す誤差
constexpr int MATCH_TOLERANCE = 2;

bool is_explorer_section(const std::string& section) {
	return section.rfind("Window.explorer:", 0) == 0;
}

DockLayoutPlan build_dock_layout(const IniDoc& base, const std::function<bool(const std::wstring&)>& is_visible,
	POINT origin, int panel_w, int panel_h) {
	DockLayoutPlan plan;
	plan.doc = base;
	int i = 0;
	for (auto& s : plan.doc.sections) {
		if (s.name.rfind("Window.", 0) != 0) continue;
		if (is_explorer_section(s.name)) {
			// 切り離し不可。結合のまま非表示にする
			s.set("hide", "1");
			continue;
		}
		std::wstring key = utf8_to_wide(s.name.substr(7));
		POINT pt = { origin.x + (i % SLOTS_PER_ROW) * SLOT_STEP, origin.y + (i / SLOTS_PER_ROW) * SLOT_STEP };
		bool visible = is_visible(key);
		s.set("hide", visible ? "0" : "1");
		s.set("floating", "1");
		s.set("x", std::to_string(pt.x));
		s.set("y", std::to_string(pt.y));
		s.set("w", std::to_string(panel_w));
		s.set("h", std::to_string(panel_h));
		plan.slots.push_back({ key, pt });
		if (visible) plan.visible_keys.push_back(key);
		i++;
	}
	return plan;
}

std::wstring match_slot(const std::vector<LayoutSlot>& slots, POINT pos) {
	for (auto& s : slots) {
		if (std::abs(s.pt.x - pos.x) <= MATCH_TOLERANCE && std::abs(s.pt.y - pos.y) <= MATCH_TOLERANCE) return s.key;
	}
	return {};
}

} // namespace tpgd
