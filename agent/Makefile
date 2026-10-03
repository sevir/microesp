# microesp-agent
BINARY   := microesp-agent
PKG      := ./cmd/microesp-agent
VERSION  ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
LDFLAGS  := -s -w -X main.version=$(VERSION)
GOFLAGS  ?= -trimpath
COVER_MIN ?= 70
DIST     := dist

.PHONY: all build cross test race lint vet staticcheck cover fmt clean install-dry-run

all: lint test build

build:
	CGO_ENABLED=0 go build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(BINARY) $(PKG)

cross:
	CGO_ENABLED=0 GOOS=linux   GOARCH=amd64 go build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(DIST)/$(BINARY)-linux-amd64 $(PKG)
	CGO_ENABLED=0 GOOS=linux   GOARCH=arm64 go build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(DIST)/$(BINARY)-linux-arm64 $(PKG)
	CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(DIST)/$(BINARY)-windows-amd64.exe $(PKG)

test:
	go test -race ./...

lint: vet staticcheck
	@test -z "$$(gofmt -l .)" || { echo "gofmt:"; gofmt -l .; exit 1; }
	@if command -v shellcheck >/dev/null; then shellcheck deploy/install.sh; else echo "shellcheck no disponible, se omite"; fi
	bash -n deploy/install.sh

vet:
	go vet ./...
	GOOS=windows go vet ./...

staticcheck:
	@if command -v staticcheck >/dev/null && staticcheck ./... >/dev/null 2>&1; then echo "staticcheck OK"; \
	else echo "staticcheck local ausente o incompatible; usando go run"; \
	go run honnef.co/go/tools/cmd/staticcheck@latest ./...; fi

# Cobertura de internal/... (incluye código ejercitado desde otros paquetes).
cover:
	go test -race -coverpkg=./internal/... -coverprofile=coverage.out ./...
	@go tool cover -func=coverage.out | tail -1
	@go tool cover -func=coverage.out | awk -v min=$(COVER_MIN) '/^total:/ { sub("%","",$$3); if ($$3+0 < min) { printf "cobertura %s%% < %s%%\n", $$3, min; exit 1 } }'

fmt:
	gofmt -w .

install-dry-run:
	./deploy/install.sh --dry-run

clean:
	rm -rf $(BINARY) $(DIST) coverage.out
