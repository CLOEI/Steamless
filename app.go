package main

import (
	_ "embed"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"

	tea "github.com/charmbracelet/bubbletea"
	"golang.org/x/sys/windows/registry"
)

//go:embed hid.dll
var hidDll []byte

type model struct {
	manifests        []string
	cursor           int
	steamPath        string
	steamlessEnabled bool
	message          string
}

func initialModel() model {
	steamPath, err := getSteamPath()
	if err != nil {
		steamPath = "Error: " + err.Error()
	}

	manifests := getManifests()

	return model{
		manifests:        manifests,
		cursor:           0,
		steamPath:        steamPath,
		steamlessEnabled: true,
		message:          "Ready",
	}
}

func (m model) Init() tea.Cmd {
	return nil
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+c", "q":
			return m, tea.Quit

		case "up", "k":
			if m.cursor > 0 {
				m.cursor--
			}

		case "down", "j":
			if m.cursor < len(m.manifests)-1 {
				m.cursor++
			}

		case "t":
			m.steamlessEnabled = !m.steamlessEnabled
			err := setSteamtoolsRegistry(m.steamlessEnabled)
			if err != nil {
				m.message = "Error setting registry: " + err.Error()
			} else {
				status := "disabled"
				if m.steamlessEnabled {
					status = "enabled"
				}
				m.message = "Steamless " + status
			}

		case "enter", " ":
			if len(m.manifests) > 0 && m.cursor < len(m.manifests) {
				err := deployManifest(m.manifests[m.cursor], m.steamPath)
				if err != nil {
					m.message = "Error: " + err.Error()
				} else {
					m.message = "Deployed " + m.manifests[m.cursor] + ", restarting Steam..."
					go restartSteam()
				}
			}
		}
	}

	return m, nil
}

func (m model) View() string {
	s := "\n"
	s += "  ███████╗████████╗███████╗ █████╗ ███╗   ███╗██╗     ███████╗███████╗███████╗\n"
	s += "  ██╔════╝╚══██╔══╝██╔════╝██╔══██╗████╗ ████║██║     ██╔════╝██╔════╝██╔════╝\n"
	s += "  ███████╗   ██║   █████╗  ███████║██╔████╔██║██║     █████╗  ███████╗███████╗\n"
	s += "  ╚════██║   ██║   ██╔══╝  ██╔══██║██║╚██╔╝██║██║     ██╔══╝  ╚════██║╚════██║\n"
	s += "  ███████║   ██║   ███████╗██║  ██║██║ ╚═╝ ██║███████╗███████╗███████║███████║\n"
	s += "  ╚══════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚══════╝╚══════╝╚══════╝\n"
	s += "\n"

	s += fmt.Sprintf("  Steam Path: %s\n", m.steamPath)

	status := "❌ Disabled"
	if m.steamlessEnabled {
		status = "✅ Enabled"
	}
	s += fmt.Sprintf("  Steamless: %s (Press 't' to toggle)\n\n", status)

	s += "  Available Manifests:\n\n"

	if len(m.manifests) == 0 {
		s += "    No manifests found in ./manifests/ folder\n"
	} else {
		for i, manifest := range m.manifests {
			cursor := " "
			if m.cursor == i {
				cursor = ">"
			}
			s += fmt.Sprintf("    %s %s\n", cursor, manifest)
		}
	}

	s += "\n"
	s += fmt.Sprintf("  Status: %s\n\n", m.message)
	s += "  Use ↑/↓ or j/k to navigate, Enter/Space to deploy, 't' to toggle, 'q' to quit\n"

	return s
}

func getSteamPath() (string, error) {
	key, err := registry.OpenKey(registry.CURRENT_USER, `Software\Valve\Steam`, registry.QUERY_VALUE)
	if err != nil {
		return "", fmt.Errorf("failed to open Steam registry key: %v", err)
	}
	defer key.Close()

	steamPath, _, err := key.GetStringValue("SteamPath")
	if err != nil {
		return "", fmt.Errorf("failed to read SteamPath value: %v", err)
	}

	return steamPath, nil
}

func getManifests() []string {
	manifestsDir := "./manifests"
	entries, err := os.ReadDir(manifestsDir)
	if err != nil {
		return []string{}
	}

	var manifests []string
	for _, entry := range entries {
		if entry.IsDir() {
			manifests = append(manifests, entry.Name())
		}
	}

	return manifests
}

func setSteamtoolsRegistry(enabled bool) error {
	registryPath := `Software\Valve\Steamtools`

	key, _, err := registry.CreateKey(registry.CURRENT_USER, registryPath, registry.SET_VALUE)
	if err != nil {
		return fmt.Errorf("failed to create Steamtools registry key: %v", err)
	}
	defer key.Close()

	enabledValue := "false"
	if enabled {
		enabledValue = "true"
	}

	if err := key.SetStringValue("ActivateUnlockMode", enabledValue); err != nil {
		return fmt.Errorf("failed to set ActivateUnlockMode: %v", err)
	}

	if err := key.SetStringValue("AlwaysStayUnlocked", enabledValue); err != nil {
		return fmt.Errorf("failed to set AlwaysStayUnlocked: %v", err)
	}

	if err := key.SetStringValue("FloatingVisible", "false"); err != nil {
		return fmt.Errorf("failed to set FloatingVisible: %v", err)
	}

	if err := key.SetStringValue("notUnlockDepot", "false"); err != nil {
		return fmt.Errorf("failed to set notUnlockDepot: %v", err)
	}

	return nil
}

func deployManifest(manifestName, steamPath string) error {
	if err := checkAndCreateHidDll(steamPath); err != nil {
		return err
	}

	if err := checkAndCreateStPluginFolder(steamPath); err != nil {
		return err
	}

	srcPath := filepath.Join("./manifests", manifestName)
	dstPath := filepath.Join(steamPath, "config", "stplug-in")

	return copyDir(srcPath, dstPath)
}

func copyDir(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}

		dstPath := filepath.Join(dst, relPath)

		if info.IsDir() {
			return os.MkdirAll(dstPath, info.Mode())
		}

		return copyFile(path, dstPath)
	})
}

func copyFile(src, dst string) error {
	srcFile, err := os.Open(src)
	if err != nil {
		return err
	}
	defer srcFile.Close()

	dstFile, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer dstFile.Close()

	_, err = io.Copy(dstFile, srcFile)
	return err
}

func checkAndCreateHidDll(steamPath string) error {
	hidDllPath := filepath.Join(steamPath, "hid.dll")

	if _, err := os.Stat(hidDllPath); err == nil {
		return nil
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("error checking if hid.dll exists: %v", err)
	}

	err := os.WriteFile(hidDllPath, hidDll, 0644)
	if err != nil {
		return fmt.Errorf("failed to create hid.dll: %v", err)
	}

	return nil
}

func checkAndCreateStPluginFolder(steamPath string) error {
	stPluginPath := filepath.Join(steamPath, "config", "stplug-in")

	if _, err := os.Stat(stPluginPath); err == nil {
		return nil
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("error checking if stplug-in folder exists: %v", err)
	}

	err := os.MkdirAll(stPluginPath, 0755)
	if err != nil {
		return fmt.Errorf("failed to create stplug-in folder: %v", err)
	}

	return nil
}

func restartSteam() {
	// Kill Steam processes
	exec.Command("taskkill", "/F", "/IM", "steam.exe").Run()
	exec.Command("taskkill", "/F", "/IM", "steamwebhelper.exe").Run()

	// Wait a moment
	// time.Sleep(2 * time.Second)

	// Find and restart Steam
	steamPath, err := getSteamPath()
	if err != nil {
		return
	}

	steamExe := filepath.Join(steamPath, "steam.exe")
	if _, err := os.Stat(steamExe); err == nil {
		exec.Command(steamExe).Start()
	}
}

func main() {
	if err := os.MkdirAll("./manifests", 0755); err != nil {
		log.Fatal(err)
	}

	p := tea.NewProgram(initialModel())
	if _, err := p.Run(); err != nil {
		fmt.Printf("Error running program: %v", err)
		os.Exit(1)
	}
}
