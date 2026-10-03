// Command microesp-agent links this PC with a MicroESP USB dongle:
// it reports telemetry and executes signed shutdown/reboot commands.
package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"os"
	"os/signal"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/microesp/agent/internal/config"
	"github.com/microesp/agent/internal/link"
	"github.com/microesp/agent/internal/power"
	"github.com/microesp/agent/internal/telemetry"
)

// version is injected with -ldflags "-X main.version=...".
var version = "dev"

// deps are the side-effecting dependencies, replaceable in tests.
type deps struct {
	stdin       io.Reader
	stdout      io.Writer
	stderr      io.Writer
	getenv      config.Getenv
	list        link.Lister
	open        link.Opener
	newExecutor func(backend string, dryRun bool, log *slog.Logger) (power.Executor, error)
	collector   func(disks []string, log *slog.Logger) telemetry.Collector
	hostInfo    func() (telemetry.HostInfo, error)
	ctx         func() (context.Context, context.CancelFunc)
}

func realDeps() deps {
	return deps{
		stdin:       os.Stdin,
		stdout:      os.Stdout,
		stderr:      os.Stderr,
		getenv:      os.Getenv,
		list:        link.SerialLister,
		open:        link.SerialOpener,
		newExecutor: power.New,
		collector: func(disks []string, log *slog.Logger) telemetry.Collector {
			return telemetry.NewSystem(disks, log)
		},
		hostInfo: telemetry.ReadHostInfo,
		ctx: func() (context.Context, context.CancelFunc) {
			return signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		},
	}
}

func main() {
	os.Exit(runMain(os.Args[1:], realDeps()))
}

const usage = `Uso: microesp-agent [run|pair|status|version] [opciones]

  run      (por defecto) mantiene la sesión con el dongle
  pair     empareja con el dongle (pide el código de 6 dígitos)
  status   busca el dongle y muestra información
  version  muestra la versión

Opciones comunes:
`

// common holds the flags shared by run/pair/status.
type common struct {
	configPath string
	device     string
	keyFile    string
	backend    string
	logLevel   string
	dryRun     boolFlag
}

// boolFlag records whether it was set explicitly.
type boolFlag struct{ set, val bool }

func (b *boolFlag) String() string   { return fmt.Sprint(b.val) }
func (b *boolFlag) IsBoolFlag() bool { return true }
func (b *boolFlag) Set(s string) error {
	switch strings.ToLower(s) {
	case "true", "1", "yes":
		b.val = true
	case "false", "0", "no":
		b.val = false
	default:
		return fmt.Errorf("valor booleano inválido %q", s)
	}
	b.set = true
	return nil
}

func (c *common) register(fs *flag.FlagSet) {
	fs.StringVar(&c.configPath, "config", "", "fichero de configuración (por defecto "+config.DefaultPath+")")
	fs.StringVar(&c.device, "device", "", `puerto serie o "auto"`)
	fs.StringVar(&c.keyFile, "key-file", "", "fichero de clave compartida")
	fs.StringVar(&c.backend, "power-backend", "", "systemd | logind-dbus | windows")
	fs.StringVar(&c.logLevel, "log-level", "", "debug | info | warn | error")
	fs.Var(&c.dryRun, "dry-run", "no ejecutar apagados/reinicios, solo registrarlos")
}

// load builds the effective config: defaults < file < env < flags.
func (c *common) load(d deps) (config.Config, error) {
	cfg, err := config.Load(c.configPath)
	if err != nil {
		return cfg, err
	}
	if err := cfg.ApplyEnv(d.getenv); err != nil {
		return cfg, err
	}
	if c.device != "" {
		cfg.Device = c.device
	}
	if c.keyFile != "" {
		cfg.KeyFile = c.keyFile
	}
	if c.backend != "" {
		cfg.PowerBackend = c.backend
	}
	if c.logLevel != "" {
		cfg.LogLevel = c.logLevel
	}
	if c.dryRun.set {
		cfg.DryRun = c.dryRun.val
	}
	return cfg, cfg.Validate()
}

func newLogger(w io.Writer, level string) *slog.Logger {
	var l slog.Level
	_ = l.UnmarshalText([]byte(level))
	return slog.New(slog.NewTextHandler(w, &slog.HandlerOptions{Level: l}))
}

func runMain(args []string, d deps) int {
	cmd := "run"
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		cmd, args = args[0], args[1:]
	}
	fs := flag.NewFlagSet("microesp-agent "+cmd, flag.ContinueOnError)
	fs.SetOutput(d.stderr)
	fs.Usage = func() {
		fmt.Fprint(d.stderr, usage)
		fs.PrintDefaults()
	}
	var c common
	var code string
	var owner string
	var handshake bool
	switch cmd {
	case "run":
		c.register(fs)
	case "pair":
		c.register(fs)
		fs.StringVar(&code, "code", "", "código de 6 dígitos (si se omite se pide por teclado)")
		fs.StringVar(&owner, "owner", "microesp", "usuario propietario del fichero de clave al ejecutar como root")
	case "status":
		c.register(fs)
		fs.BoolVar(&handshake, "handshake", false, "abrir el puerto y autenticar la sesión (detén antes el servicio)")
	case "version", "-v", "--version":
		fmt.Fprintf(d.stdout, "microesp-agent %s (%s, %s/%s)\n", version, runtime.Version(), runtime.GOOS, runtime.GOARCH)
		return 0
	case "help", "-h", "--help":
		fs.Usage()
		return 0
	default:
		fmt.Fprintf(d.stderr, "subcomando desconocido %q\n", cmd)
		fs.Usage()
		return 2
	}
	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return 0
		}
		return 2
	}
	cfg, err := c.load(d)
	if err != nil {
		fmt.Fprintf(d.stderr, "configuración inválida: %v\n", err)
		return 2
	}
	log := newLogger(d.stderr, cfg.LogLevel)
	switch cmd {
	case "pair":
		return cmdPair(d, cfg, code, owner, log)
	case "status":
		return cmdStatus(d, cfg, handshake, log)
	}
	return cmdRun(d, cfg, log)
}

func osName() string {
	if runtime.GOOS == "windows" {
		return "windows"
	}
	return "linux"
}

func newAgent(d deps, cfg config.Config, log *slog.Logger) (*link.Agent, error) {
	ex, err := d.newExecutor(cfg.PowerBackend, cfg.DryRun, log)
	if err != nil {
		return nil, err
	}
	return &link.Agent{
		Device:            cfg.Device,
		List:              d.list,
		Open:              d.open,
		LoadKey:           func() ([]byte, error) { return config.LoadKey(cfg.KeyFile) },
		HostInfo:          d.hostInfo,
		Collector:         d.collector(cfg.Disks, log),
		Executor:          ex,
		Version:           version,
		OS:                osName(),
		TelemetryInterval: cfg.TelemetryInterval.Duration,
		HeartbeatInterval: cfg.HeartbeatInterval.Duration,
		Log:               log,
	}, nil
}

func cmdRun(d deps, cfg config.Config, log *slog.Logger) int {
	a, err := newAgent(d, cfg, log)
	if err != nil {
		log.Error("cannot start", "err", err)
		return 1
	}
	log.Info("microesp-agent starting", "version", version, "device", cfg.Device, "backend", a.Executor.Name(), "dry_run", cfg.DryRun)
	ctx, cancel := d.ctx()
	defer cancel()
	if err := runService(ctx, a.Run); err != nil {
		log.Error("agent stopped", "err", err)
		return 1
	}
	log.Info("microesp-agent stopped")
	return 0
}

func openDevice(d deps, cfg config.Config) (string, *link.Conn, error) {
	path, err := link.Find(d.list, cfg.Device)
	if err != nil {
		return "", nil, err
	}
	rw, err := d.open(path)
	if err != nil {
		return path, nil, fmt.Errorf("abrir %s: %w (¿servicio en marcha? sudo systemctl stop microesp-agent)", path, err)
	}
	return path, link.NewConn(rw, nil), nil
}

func cmdPair(d deps, cfg config.Config, code, owner string, log *slog.Logger) int {
	if code == "" {
		fmt.Fprint(d.stdout, "Introduce el código de 6 dígitos que muestra el dongle: ")
		line, err := bufio.NewReader(d.stdin).ReadString('\n')
		if err != nil && line == "" {
			fmt.Fprintf(d.stderr, "\nno se pudo leer el código: %v\n", err)
			return 1
		}
		code = strings.TrimSpace(line)
	}
	if len(code) != 6 || strings.Trim(code, "0123456789") != "" {
		fmt.Fprintln(d.stderr, "el código debe tener exactamente 6 dígitos")
		return 2
	}
	path, c, err := openDevice(d, cfg)
	if err != nil {
		fmt.Fprintf(d.stderr, "error: %v\n", err)
		return 1
	}
	defer c.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	key, err := link.Pair(ctx, c, code, 10*time.Second)
	if err != nil {
		if link.IsDongleError(err, "pair_failed") {
			fmt.Fprintln(d.stderr, "emparejado rechazado: código incorrecto o el dongle no está en modo emparejado")
		} else {
			fmt.Fprintf(d.stderr, "emparejado fallido: %v\n", err)
		}
		return 1
	}
	if err := config.SaveKey(cfg.KeyFile, key, owner); err != nil {
		fmt.Fprintf(d.stderr, "no se pudo guardar la clave en %s: %v\n", cfg.KeyFile, err)
		return 1
	}
	log.Info("paired", "port", path, "key_file", cfg.KeyFile)
	fmt.Fprintf(d.stdout, "Emparejado correctamente con %s. Clave guardada en %s.\n", path, cfg.KeyFile)
	fmt.Fprintln(d.stdout, "Reinicia el servicio: sudo systemctl restart microesp-agent")
	return 0
}

func cmdStatus(d deps, cfg config.Config, handshake bool, log *slog.Logger) int {
	out := d.stdout
	fmt.Fprintf(out, "microesp-agent %s\n", version)
	fmt.Fprintf(out, "dispositivo configurado: %s\n", cfg.Device)
	fmt.Fprintf(out, "backend de energía:      %s (dry_run=%v)\n", cfg.PowerBackend, cfg.DryRun)
	if _, err := config.LoadKey(cfg.KeyFile); err != nil {
		fmt.Fprintf(out, "clave:                   NO VÁLIDA (%v)\n", err)
	} else {
		fmt.Fprintf(out, "clave:                   OK (%s)\n", cfg.KeyFile)
	}
	rc := 0
	if cs, err := link.Candidates(d.list); err != nil {
		fmt.Fprintf(out, "enumeración USB:         error: %v\n", err)
	} else if len(cs) == 0 {
		fmt.Fprintln(out, "dongles MicroESP:        ninguno detectado (VID 303a / PID 4002)")
		if cfg.Device == config.DeviceAuto {
			rc = 1
		}
	} else {
		for _, p := range cs {
			fmt.Fprintf(out, "dongle:                  %s  %s:%s  serie=%s  producto=%q\n", p.Name, p.VID, p.PID, p.Serial, p.Product)
		}
	}
	if !handshake || rc != 0 {
		return rc
	}
	key, err := config.LoadKey(cfg.KeyFile)
	if err != nil {
		return 1
	}
	path, c, err := openDevice(d, cfg)
	if err != nil {
		fmt.Fprintf(out, "sesión:                  error: %v\n", err)
		return 1
	}
	defer c.Close()
	hi, _ := d.hostInfo()
	s, err := link.Handshake(context.Background(), c, key,
		link.HelloParams{Host: hi.Hostname, OS: osName(), AgentVer: version, MACs: hi.MACs}, link.HandshakeTimeout)
	if err != nil {
		fmt.Fprintf(out, "sesión:                  FALLO en %s: %v\n", path, err)
		return 1
	}
	log.Debug("handshake ok", "port", path)
	fmt.Fprintf(out, "sesión:                  OK en %s (fw %s, dev %s)\n", path, s.Welcome.FW, s.Welcome.Dev)
	return 0
}
