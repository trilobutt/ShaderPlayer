#pragma once

// SpoutPanel.h — the body of the Spout Output dock.
//
// One question justifies this panel: is the picture leaving this machine, and under what
// name. Everything else here is in service of answering it at a glance.
//
//   The state is the largest thing in the dock. SENDING, WAITING, UNAVAILABLE and OFF are
//   four visibly different surfaces, not four spellings of a small grey caption: the word
//   is set large in its own state colour, and while the sender is live the panel carries a
//   steady lit border, the same language the Recording dock uses for "this is going out".
//
//   A name collision is reported rather than swallowed. spoutDX silently appends a suffix
//   when the requested name is already registered by another application, so the panel
//   compares what was asked for against what is actually in shared memory and says which
//   name a receiver has to look for when they differ.
//
//   A failure to start is reported too. Spout initialises at launch and may not; enabling
//   it then leaves AppConfig::spoutEnabled true with nothing behind it, which without this
//   panel is silent.
//
// The state changes with no signal to hang off (IsSpoutActive only flips once a frame has
// been sent), so Tick polls it. It is a compare against the last applied state, and it
// restyles only when that comparison fails.

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPaintEvent;
class QVBoxLayout;
QT_END_NAMESPACE

namespace SP {

class Application;

class SpoutPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpoutPanel(Application& app, QWidget* parent = nullptr);

    // Once per frame: the sender's live state and the name it actually registered under.
    // Cheap, and does nothing at all while the dock is hidden.
    void Tick();

protected:
    // The lit border carried while the sender is live. The dock's own frame and glow belong
    // to RegionDock and are left alone.
    void paintEvent(QPaintEvent* event) override;

private:
    // Four states, in the order a user meets them.
    enum class State { Off, Unavailable, Waiting, Sending };

    void BuildStatus(QVBoxLayout* layout);
    void BuildSender(QVBoxLayout* layout);
    void BuildReceivers(QVBoxLayout* layout);

    State CurrentState() const;
    void SyncStatus();
    void ApplySenderName();

    Application& m_app;

    QLabel* m_stateWord = nullptr;
    QWidget* m_dot = nullptr;
    QLabel* m_stateDetail = nullptr;
    QLabel* m_collision = nullptr;     // hidden unless the registered name was renamed
    QCheckBox* m_enable = nullptr;
    QLineEdit* m_name = nullptr;

    // Last applied, so the per-frame poll is a compare rather than a restyle. m_synced is
    // false until the first apply, which is what makes the opening state paint itself.
    State m_state = State::Off;
    QString m_activeName;
    bool m_synced = false;
};

}  // namespace SP
