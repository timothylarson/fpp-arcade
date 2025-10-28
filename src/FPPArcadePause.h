#ifndef __FPPARCADE_PAUSE__
#define __FPPARCADE_PAUSE__

enum class PauseMenuOption {
    Resume,
    Restart
};

class PauseMenu {
public:
    PauseMenu() : selectedOption(PauseMenuOption::Resume) {}

    void moveUp() {
        selectedOption = PauseMenuOption::Resume;
    }

    void moveDown() {
        selectedOption = PauseMenuOption::Restart;
    }

    bool isResumeSelected() const {
        return selectedOption == PauseMenuOption::Resume;
    }

    PauseMenuOption getSelectedOption() const {
        return selectedOption;
    }

private:
    PauseMenuOption selectedOption;
};

#endif