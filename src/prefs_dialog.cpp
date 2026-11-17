#include <wx/dirctrl.h>
#include <wx/notebook.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/wx.h>

#include "v_radar_pi.h"

namespace {

class PrefsDialog : public wxDialog {
 public:
  PrefsDialog(wxWindow* parent, VRadarPi& pi)
      : wxDialog(parent, wxID_ANY, _("V-RADAR settings"), wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_pi(pi) {
    const VRadarSettings& s = pi.settings();
    auto* top = new wxBoxSizer(wxVERTICAL);
    auto* book = new wxNotebook(this, wxID_ANY);

    // one panel per group; controls are children of their page
    auto page = [&](const wxString& title) {
      auto* p = new wxPanel(book);
      auto* sz = new wxBoxSizer(wxVERTICAL);
      p->SetSizer(sz);
      book->AddPage(p, title);
      return std::make_pair(p, sz);
    };
    auto label = [](wxWindow* p, const wxString& t) { return new wxStaticText(p, wxID_ANY, t); };

    // --- status (first tab) ---
    {
      auto [p, sz] = page(_("Status"));
      m_status = new wxStaticText(p, wxID_ANY, StatusText());
      sz->Add(m_status, 0, wxALL, 8);
    }
    // --- own ship ---
    {
      auto [p, sz] = page(_("Own ship"));
      wxString modes[] = {_("Follow OpenCPN position (GPS, ShipDriver, ...)"), _("Fixed position")};
      m_mode = new wxRadioBox(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 2, modes, 1);
      m_mode->SetSelection(s.use_fixed_position ? 1 : 0);
      sz->Add(m_mode, 0, wxALL | wxEXPAND, 8);
      auto* grid = new wxFlexGridSizer(2, 6, 6);
      auto addNum = [&](const wxString& l, double v, wxTextCtrl*& out) {
        grid->Add(label(p, l), 0, wxALIGN_CENTER_VERTICAL);
        out = new wxTextCtrl(p, wxID_ANY, wxString::Format("%.5f", v), wxDefaultPosition, wxSize(110, -1));
        grid->Add(out);
      };
      addNum(_("Latitude (deg, N positive)"), s.fixed_lat, m_lat);
      addNum(_("Longitude (deg, E positive)"), s.fixed_lon, m_lon);
      addNum(_("Heading (deg true)"), s.fixed_heading, m_hdg);
      sz->Add(grid, 0, wxALL, 8);
    }
    // --- other ships ---
    {
      auto [p, sz] = page(_("Ships"));
      m_ais = new wxCheckBox(p, wxID_ANY, _("Simulated ships (AIS targets and radar echoes)"));
      m_ais->SetValue(s.ais_enabled);
      sz->Add(m_ais, 0, wxALL, 8);
      auto* g = new wxFlexGridSizer(2, 6, 6);
      g->Add(label(p, _("Number of ships")), 0, wxALIGN_CENTER_VERTICAL);
      m_count = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 60, s.ship_count);
      g->Add(m_count);
      g->Add(label(p, _("Scenario number (same number = same ships)")), 0, wxALIGN_CENTER_VERTICAL);
      m_seed = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 999999, s.seed);
      g->Add(m_seed);
      sz->Add(g, 0, wxALL, 8);
    }
    // --- charts ---
    {
      auto [p, sz] = page(_("Charts"));
      auto* g = new wxFlexGridSizer(2, 6, 6);
      g->Add(label(p, _("Radius around ship (NM)")), 0, wxALIGN_CENTER_VERTICAL);
      m_radius = new wxSpinCtrlDouble(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 3, 12, s.enc_radius_nm, 1);
      g->Add(m_radius);
      sz->Add(g, 0, wxALL, 8);
      sz->Add(label(p, _("S-57 ENC cells are found in the chart directories of OpenCPN.\nExtra folder (optional):")), 0, wxLEFT | wxRIGHT, 8);
      m_dir = new wxDirPickerCtrl(p, wxID_ANY, s.enc_dir, _("Select ENC folder"), wxDefaultPosition, wxSize(380, -1), wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
      sz->Add(m_dir, 0, wxALL | wxEXPAND, 8);
    }
    // --- weather ---
    {
      auto [p, sz] = page(_("Weather"));
      wxString srcs[] = {_("Manual"), _("GRIB (loaded in the GRIB plugin)")};
      m_wsrc = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, srcs);
      m_wsrc->SetSelection(s.weather_source == 1 ? 1 : 0);
      sz->Add(m_wsrc, 0, wxALL | wxEXPAND, 8);
      auto* g = new wxFlexGridSizer(2, 6, 6);
      g->Add(label(p, _("Wind speed (kn)")), 0, wxALIGN_CENTER_VERTICAL);
      m_wkn = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 60, s.wind_kn);
      g->Add(m_wkn);
      g->Add(label(p, _("Wind from (deg)")), 0, wxALIGN_CENTER_VERTICAL);
      m_wdir = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 359, s.wind_dir);
      g->Add(m_wdir);
      g->Add(label(p, _("Wave height (m)")), 0, wxALIGN_CENTER_VERTICAL);
      m_wave = new wxSpinCtrlDouble(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 6, s.wave_m, 0.1);
      g->Add(m_wave);
      g->Add(label(p, _("Rain")), 0, wxALIGN_CENTER_VERTICAL);
      wxString rains[] = {_("None"), _("Light showers"), _("Heavy showers"), _("Squall line")};
      m_rain = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, 4, rains);
      m_rain->SetSelection(std::max(0, std::min(3, s.rain_preset)));
      g->Add(m_rain);
      g->Add(label(p, _("Other radars nearby (interference)")), 0, wxALIGN_CENTER_VERTICAL);
      wxString ifs[] = {_("None"), _("Some"), _("Many")};
      m_intf = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, 3, ifs);
      m_intf->SetSelection(std::max(0, std::min(2, s.interference)));
      g->Add(m_intf);
      sz->Add(g, 0, wxALL, 8);
      sz->Add(label(p, _("Clutter is removed with the GAIN, SEA and RAIN controls of the radar;\ninterference with Interference rejection.")), 0, wxALL, 8);
    }
    // --- radar and status ---
    {
      auto [p, sz] = page(_("Radar"));
      m_tx = new wxCheckBox(p, wxID_ANY, _("Transmit immediately at start (applies after restarting OpenCPN)"));
      m_tx->SetValue(s.start_transmitting);
      sz->Add(m_tx, 0, wxALL, 8);
      m_share = new wxCheckBox(p, wxID_ANY, _("Share the radar with other computers on the network"));
      m_share->SetValue(s.share_network);
      sz->Add(m_share, 0, wxLEFT | wxRIGHT, 8);
      sz->Add(label(p, _("Off (default): the radar stays on this computer. On: other OpenCPN/plotters can use it, but the\nradar stream is several Mbit/s of multicast and can overload Wi-Fi: use a wired network.")), 0, wxALL, 8);
    }

    top->Add(book, 1, wxALL | wxEXPAND, 6);
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxALIGN_RIGHT, 8);
    SetSizerAndFit(top);
    m_mode->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) { UpdateEnabled(); });
    m_ais->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateEnabled(); });
    m_wsrc->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateEnabled(); });
    UpdateEnabled();
  }

  bool Collect(VRadarSettings& out) {
    double lat, lon, hdg;
    if (!m_lat->GetValue().ToDouble(&lat) || !m_lon->GetValue().ToDouble(&lon) || !m_hdg->GetValue().ToDouble(&hdg) ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      wxMessageBox(_("Latitude, longitude and heading must be numbers (degrees)."), _("V-RADAR"), wxOK | wxICON_WARNING, this);
      return false;
    }
    out = m_pi.settings();
    out.use_fixed_position = m_mode->GetSelection() == 1;
    out.fixed_lat = lat; out.fixed_lon = lon;
    out.fixed_heading = std::fmod(std::fmod(hdg, 360.0) + 360.0, 360.0);
    out.ais_enabled = m_ais->GetValue();
    out.ship_count = m_count->GetValue();
    out.seed = m_seed->GetValue();
    out.enc_radius_nm = m_radius->GetValue();
    out.enc_dir = m_dir->GetPath();
    out.start_transmitting = m_tx->GetValue();
    out.share_network = m_share->GetValue();
    out.weather_source = m_wsrc->GetSelection();
    out.wind_kn = m_wkn->GetValue();
    out.wind_dir = m_wdir->GetValue();
    out.wave_m = m_wave->GetValue();
    out.rain_preset = m_rain->GetSelection();
    out.interference = m_intf->GetSelection();
    return true;
  }

 private:
  wxString StatusText() {
    VRadarEngine::Status s = m_pi.EngineStatus();
    wxString t;
    if (s.cells_found == 0 && s.region_ready)
      t = _("No ENC cells (*.000) found in the chart directories: the radar will show no land.\n");
    else
      t.Printf(_("ENC cells: %d found, %d used around the ship%s\n"), s.cells_found, s.cells_used,
               s.extracting ? _(" (loading...)") : wxString());
    t += wxString::Format(_("Simulated ships: %d\nRadar: %s, range %d m\nNetwork: %s\nWeather: %s"), s.vessels,
                          s.transmitting ? _("transmitting") : _("standby"), s.range_m,
                          s.nics > 0 ? wxString::Format(s.sharing ? _("%d interface(s), shared on the network") : _("%d interface(s), this computer only"), s.nics) : wxString(_("none: connect to a network (the radar plugin needs one)")),
                          wxString::FromUTF8(s.weather.c_str()));
    return t;
  }
  void UpdateEnabled() {
    bool fixed = m_mode->GetSelection() == 1;
    m_lat->Enable(fixed); m_lon->Enable(fixed); m_hdg->Enable(fixed);
    bool ais = m_ais->GetValue();
    m_count->Enable(ais); m_seed->Enable(ais);
    bool manual = m_wsrc->GetSelection() == 0;
    m_wkn->Enable(manual); m_wdir->Enable(manual); m_wave->Enable(manual); m_rain->Enable(manual);
  }

  VRadarPi& m_pi;
  wxRadioBox* m_mode;
  wxTextCtrl *m_lat, *m_lon, *m_hdg;
  wxCheckBox *m_ais, *m_tx, *m_share;
  wxSpinCtrl *m_count, *m_seed;
  wxSpinCtrlDouble* m_radius;
  wxDirPickerCtrl* m_dir;
  wxStaticText* m_status;
  wxChoice *m_wsrc, *m_rain, *m_intf;
  wxSpinCtrl *m_wkn, *m_wdir;
  wxSpinCtrlDouble* m_wave;
};

}  // namespace

void VRadarPi::ShowPreferencesDialog(wxWindow* parent) {
  PrefsDialog dlg(parent, *this);
  if (dlg.ShowModal() != wxID_OK) return;
  VRadarSettings s;
  if (dlg.Collect(s)) SetSettings(s);
}
