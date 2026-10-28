#ifndef __FPPARCADE_PAUSE__
#define __FPPARCADE_PAUSE__

// The pause menu's entries, top to bottom.
enum class PauseMenuOption {
    Resume,
    Restart,
    Exit
};

class PauseMenu {
public:
    PauseMenu() : selectedOption(PauseMenuOption::Resume) {}

    void reset() {
        selectedOption = PauseMenuOption::Resume;
    }

    void moveUp() {
        if (selectedOption != PauseMenuOption::Resume) {
            selectedOption = (PauseMenuOption)((int)selectedOption - 1);
        }
    }

    void moveDown() {
        if (selectedOption != PauseMenuOption::Exit) {
            selectedOption = (PauseMenuOption)((int)selectedOption + 1);
        }
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
