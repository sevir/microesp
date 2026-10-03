package link

import (
	"io"
	"net"

	"github.com/microesp/agent/internal/proto"
)

func pipe() (net.Conn, net.Conn) { return net.Pipe() }

func writeMsg(w io.Writer, m proto.Message) {
	b, _ := proto.Encode(m)
	_, _ = w.Write(b)
}
