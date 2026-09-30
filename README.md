# projetoSO

Simulador de escalonamento de processos, desenvolvido em C para a disciplina de Sistemas Operacionais.

## Requisitos

- `gcc` (compilador C com suporte a C11)
- `make`

## Instalação

### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install build-essential
```

### Windows (recomendado: WSL)

1. Instale o WSL com Ubuntu, no PowerShell como administrador:

   ```powershell
   wsl --install -d Ubuntu
   ```

2. Abra o Ubuntu e instale o compilador:

   ```bash
   sudo apt update
   sudo apt install build-essential
   ```

> **Por que WSL?** No Windows 11 com o *Controle Inteligente de Aplicativos*
> (Smart App Control) ligado, executáveis `.exe` compilados localmente são
> bloqueados com a mensagem "Uma política de Controle de Aplicativo bloqueou
> este arquivo". Programas compilados no WSL não passam por esse bloqueio.

### Windows (alternativa: MSYS2)

Só funciona se o Controle Inteligente de Aplicativos estiver desativado.

1. Instale o [MSYS2](https://www.msys2.org/).
2. No terminal *MSYS2 UCRT64*:

   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc make
   ```

3. Adicione `C:\msys64\ucrt64\bin` ao `PATH` do Windows.

## Obtendo o código

```bash
git clone https://github.com/GuilhermeOliverr/projetoSO.git
cd projetoSO
```

## Compilação e execução

### Linux ou dentro do WSL

```bash
make          # compila e gera o executável projetoSO
./projetoSO   # executa
make clean    # remove o executável
```

### Windows, pelo PowerShell (usando o WSL)

Dentro da pasta do projeto, sem precisar abrir o Ubuntu:

```powershell
wsl -d Ubuntu make
wsl -d Ubuntu ./projetoSO
```

### Windows com MSYS2

```powershell
make
.\projetoSO.exe
```

Saída esperada:

```
projetoSO: simulador de escalonamento
```

## Estrutura

```
projetoSO/
├── main.c      # ponto de entrada do simulador
├── Makefile    # regras de compilação
└── README.md
```
