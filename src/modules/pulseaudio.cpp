#include "modules/pulseaudio.hpp"

#include "adapters/pulseaudio.hpp"
#include "drawtypes/iconset.hpp"
#include "drawtypes/label.hpp"
#include "drawtypes/progressbar.hpp"
#include "drawtypes/ramp.hpp"
#include "modules/meta/base.inl"
#include "settings.hpp"
#include "utils/math.hpp"

POLYBAR_NS

namespace modules {
  template class module<pulseaudio_module>;

  pulseaudio_module::pulseaudio_module(const bar_settings& bar, string name_)
      : event_module<pulseaudio_module>(bar, move(name_)) {
    // Load configuration values
    m_interval = m_conf.get(name(), "interval", m_interval);

    auto sink_name = m_conf.get(name(), "sink", ""s);
    bool m_max_volume = m_conf.get(name(), "use-ui-max", true);

    try {
      m_pulseaudio = factory_util::unique<pulseaudio>(m_log, move(sink_name), m_max_volume);
    } catch (const pulseaudio_error& err) {
      throw module_error(err.what());
    }

    m_port_icons = factory_util::shared<iconset>();

    // Add formats and elements
    m_formatter->add(FORMAT_VOLUME, TAG_LABEL_VOLUME, {TAG_RAMP_VOLUME, TAG_LABEL_VOLUME, TAG_BAR_VOLUME, TAG_ICON_PORT});
    m_formatter->add(FORMAT_MUTED, TAG_LABEL_MUTED, {TAG_RAMP_VOLUME, TAG_LABEL_MUTED, TAG_BAR_VOLUME, TAG_ICON_PORT});

    if (m_formatter->has(TAG_BAR_VOLUME)) {
      m_bar_volume = load_progressbar(m_bar, m_conf, name(), TAG_BAR_VOLUME);
    }
    if (m_formatter->has(TAG_LABEL_VOLUME, FORMAT_VOLUME)) {
      m_label_volume = load_optional_label(m_conf, name(), TAG_LABEL_VOLUME, "%percentage%%");
    }
    if (m_formatter->has(TAG_LABEL_MUTED, FORMAT_MUTED)) {
      m_label_muted = load_optional_label(m_conf, name(), TAG_LABEL_MUTED, "%percentage%%");
    }
    if (m_formatter->has(TAG_RAMP_VOLUME)) {
      m_ramp_volume = load_ramp(m_conf, name(), TAG_RAMP_VOLUME);
    }
    if (m_formatter->has(TAG_ICON_PORT)) {
      m_port_icons->add("headphones", load_optional_icon(m_conf, name(), TAG_ICON_HEADPHONES));
      m_port_icons->add("speaker", load_optional_icon(m_conf, name(), TAG_ICON_SPEAKER));
      m_port_icons->add("other", load_optional_icon(m_conf, name(), TAG_ICON_OTHER));
      m_port_icons->add("hdmi",
          load_optional_icon(m_conf, name(), TAG_ICON_HDMI, m_port_icons->get("other")->get()));
      m_port_icons->add("bt-headset",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_HEADSET, m_port_icons->get("headphones")->get()));
      m_port_icons->add("bt-handsfree",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_HANDSFREE, m_port_icons->get("headphones")->get()));
      m_port_icons->add("bt-speaker",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_SPEAKER, m_port_icons->get("speaker")->get()));
      m_port_icons->add("bt-headphones",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_HEADPHONES, m_port_icons->get("headphones")->get()));
      m_port_icons->add("bt-portable",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_PORTABLE, m_port_icons->get("other")->get()));
      m_port_icons->add("bt-car",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_CAR, m_port_icons->get("other")->get()));
      m_port_icons->add("bt-hifi",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_HIFI, m_port_icons->get("other")->get()));
      m_port_icons->add("bt-phone",
          load_optional_icon(m_conf, name(), TAG_ICON_BT_PHONE, m_port_icons->get("other")->get()));
    }
  }

  void pulseaudio_module::teardown() {
    m_pulseaudio.reset();
  }

  bool pulseaudio_module::has_event() {
    // Poll for mixer and control events
    try {
      if (m_pulseaudio->wait())
        return true;
    } catch (const pulseaudio_error& e) {
      m_log.err("%s: %s", name(), e.what());
    }
    return false;
  }

  bool pulseaudio_module::update() {
    // Consume pending events
    m_pulseaudio->process_events();

    // Get volume and mute state
    m_volume = 100;
    m_decibels = PA_DECIBEL_MININFTY;
    m_muted = false;

    try {
      if (m_pulseaudio) {
        m_volume = m_volume * m_pulseaudio->get_volume() / 100.0f;
        m_decibels = m_pulseaudio->get_decibels();
        m_muted = m_muted || m_pulseaudio->is_muted();
      }
    } catch (const pulseaudio_error& err) {
      m_log.err("%s: Failed to query pulseaudio sink (%s)", name(), err.what());
    }

    // Replace label tokens
    if (m_label_volume) {
      m_label_volume->reset_tokens();
      m_label_volume->replace_token("%decibels%", string_util::floating_point(m_decibels, 2, true));
      m_label_volume->replace_token("%port-icon%", port_icon());
    }

    if (m_label_muted) {
      m_label_muted->reset_tokens();
      m_label_muted->replace_token("%decibels%", string_util::floating_point(m_decibels, 2, true));
      m_label_volume->replace_token("%port-icon%", port_icon());
    }

    return true;
  }

  string pulseaudio_module::get_format() const {
    return m_muted ? FORMAT_MUTED : FORMAT_VOLUME;
  }

  string pulseaudio_module::get_output() {
    // Get the module output early so that
    // the format prefix/suffix also gets wrapper
    // with the cmd handlers
    string output{module::get_output()};

    if (m_handle_events) {
      auto click_middle = m_conf.get(name(), "click-middle", ""s);
      auto click_right = m_conf.get(name(), "click-right", ""s);

      if (!click_middle.empty()) {
        m_builder->cmd(mousebtn::MIDDLE, click_middle);
      }

      if (!click_right.empty()) {
        m_builder->cmd(mousebtn::RIGHT, click_right);
      }

      m_builder->cmd(mousebtn::LEFT, EVENT_TOGGLE_MUTE);
      m_builder->cmd(mousebtn::SCROLL_UP, EVENT_VOLUME_UP);
      m_builder->cmd(mousebtn::SCROLL_DOWN, EVENT_VOLUME_DOWN);
    }

    m_builder->append(output);

    return m_builder->flush();
  }

  bool pulseaudio_module::build(builder* builder, const string& tag) const {
    if (tag == TAG_BAR_VOLUME) {
      builder->node(m_bar_volume->output(m_volume));
    } else if (tag == TAG_RAMP_VOLUME) {
      builder->node(m_ramp_volume->get_by_percentage(m_volume));
    } else if (tag == TAG_LABEL_VOLUME) {
      builder->node(m_label_volume);
    } else if (tag == TAG_LABEL_MUTED) {
      builder->node(m_label_muted);
    } else if (tag == TAG_ICON_PORT) {
      auto port = m_pulseaudio->get_port_name();
      auto sink = m_pulseaudio->get_name();
      if (string_util::contains(sink, "a2dp_sink")) {
        if (string_util::contains(port, "headset")) {
          builder->node(m_port_icons->get("bt-headset"));
        } else if (string_util::contains(port, "handsfree")) {
          builder->node(m_port_icons->get("bt-handsfree"));
        } else if (string_util::contains(port, "speaker")) {
          builder->node(m_port_icons->get("bt-speaker"));
        } else if (string_util::contains(port, "headphone")) {
          builder->node(m_port_icons->get("bt-headphones"));
        } else if (string_util::contains(port, "portable")) {
          builder->node(m_port_icons->get("bt-portable"));
        } else if (string_util::contains(port, "car")) {
          builder->node(m_port_icons->get("bt-car"));
        } else if (string_util::contains(port, "hifi")) {
          builder->node(m_port_icons->get("bt-hifi"));
        } else if (string_util::contains(port, "phone")) {
          builder->node(m_port_icons->get("bt-phone"));
        }
      } else if (string_util::contains(port, "headphones")) {
        builder->node(m_port_icons->get("headphones"));
      } else if (string_util::contains(port, "speaker")) {
        builder->node(m_port_icons->get("speaker"));
      } else if (string_util::contains(port, "hdmi")) {
        builder->node(m_port_icons->get("hdmi"));
      }
    } else {
      return false;
    }
    return true;
  }

  bool pulseaudio_module::input(string&& cmd) {
    if (!m_handle_events) {
      return false;
    } else if (cmd.compare(0, strlen(EVENT_PREFIX), EVENT_PREFIX) != 0) {
      return false;
    }

    try {
      if (m_pulseaudio && !m_pulseaudio->get_name().empty()) {
        if (cmd.compare(0, strlen(EVENT_TOGGLE_MUTE), EVENT_TOGGLE_MUTE) == 0) {
          m_pulseaudio->toggle_mute();
        } else if (cmd.compare(0, strlen(EVENT_VOLUME_UP), EVENT_VOLUME_UP) == 0) {
          // cap above 100 (~150)?
          m_pulseaudio->inc_volume(m_interval);
        } else if (cmd.compare(0, strlen(EVENT_VOLUME_DOWN), EVENT_VOLUME_DOWN) == 0) {
          m_pulseaudio->inc_volume(-m_interval);
        } else {
          return false;
        }
      }
    } catch (const exception& err) {
      m_log.err("%s: Failed to handle command (%s)", name(), err.what());
    }

    return true;
  }
}  // namespace modules

POLYBAR_NS_END
