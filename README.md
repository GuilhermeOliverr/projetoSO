# projetoSO

Simulador de um sistema operacional multitarefa preemptivo com escalonadores de
tempo real **Rate Monotonic (RM)** e **Earliest Deadline First (EDF)**, de 1 a N
CPUs, desenvolvido em C11 para a disciplina de Sistemas Operacionais (Projeto A).
Tem interface gráfica em janela no Linux/WSL, no Windows e no macOS, feita só
com a biblioteca padrão do C e com o que já vem em cada sistema: não precisa
instalar nada além do compilador.

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
make                                   # compila e gera o executável projetoSO
./projetoSO                            # abre a janela e pede o arquivo
./projetoSO exemplos/fig2_5_rm.txt     # abre a janela já com o arquivo
./projetoSO --terminal                 # mesma interface, no terminal
make test                              # roda a bateria de testes
make clean                             # remove executável, plugins e SVGs gerados
```

No WSL do Windows 11 a janela aparece normalmente, porque o WSLg já vem com o
servidor gráfico. Sem servidor gráfico (ex.: WSL do Windows 10, SSH), o
programa avisa e usa o terminal sozinho.

### Windows, pelo PowerShell (usando o WSL)

Dentro da pasta do projeto, sem precisar abrir o Ubuntu:

```powershell
wsl -d Ubuntu make
wsl -d Ubuntu ./projetoSO exemplos/fig2_5_rm.txt
```

### Windows com MSYS2

```powershell
make
.\projetoSO.exe exemplos\fig2_5_rm.txt
```

A janela usa a API Win32, que faz parte do Windows. (No MSYS2 os plugins
`.so` ficam desativados; o resto funciona igual.)

### macOS

Só é preciso o compilador do sistema (`xcode-select --install`, se ainda não
tiver). A janela usa o Cocoa, que já vem no macOS.

```bash
make
./projetoSO exemplos/fig2_5_rm.txt
```

## Como usar

```
./projetoSO [arquivo_config] [opções]

  --modo passo|completo  modo de execução, sem perguntar
  --saida ARQ.svg        onde salvar o Gantt (padrão: <nome do config>_gantt.svg)
  --texto                no modo completo, imprime também a linha do tempo em texto
  --validar              só carrega e valida o arquivo (código de saída 0 = válido)
  --semente N            semente do sorteio de desempate (padrão 1; 0 = relógio)
  --plugin LIB.so        carrega um escalonador de uma biblioteca dinâmica
  --sem-cor              desliga as cores ANSI
  --terminal             usa a interface de texto em vez da janela gráfica
```

Sem `--modo`, o programa abre uma **janela gráfica**:

1. **Abrir arquivo**: digite o caminho (qualquer nome, qualquer pasta ou disco)
   ou clique num arquivo da lista (clique de novo, ou Enter, para carregar).
   Atalhos: **Pendrives e discos** (Linux: `/media/<usuário>`; Windows: lista
   de unidades C:, D:, E:...; macOS: `/Volumes`), **Pasta pessoal**, **Pasta do
   programa** e **Pasta acima**.
   Se o arquivo tiver erros, todos aparecem com linha, campo e motivo.
2. **Configuração**: mostra sistema e tarefas com os valores atuais; botões
   para trocar algoritmo, quantum e CPUs, editar/adicionar/remover tarefas ou
   abrir outro arquivo. Os formulários já vêm com os valores sugeridos e
   recusam valores inválidos dizendo o motivo.
3. **Passo a passo** ou **Execução completa**: Gantt na janela (com legenda,
   rolagem e zoom), painel do tick (CPUs, fila e eventos) e painel da tarefa
   escolhida (TCB) ou do sistema. Botões e atalhos: Avançar (→, Enter),
   Voltar (←), Início (Home), Até o fim (End), Ir para (g), Editar tarefa (e),
   \+ Tarefa (+), Algoritmo (a), Quantum (k), CPUs (c), Exportar SVG (x),
   Estatísticas (s), Ajuda (h). Clique numa tarefa do gráfico para escolhê-la
   e numa coluna para ver aquele tick.

### Interface de terminal

A interface de texto continua completa e faz o mesmo que a janela:

```bash
./projetoSO --terminal                          # pede o caminho do arquivo
./projetoSO exemplos/fig2_5_rm.txt --terminal   # já carrega o arquivo
./projetoSO exemplos/fig2_5_rm.txt --modo passo     # direto no passo a passo
./projetoSO exemplos/fig2_5_rm.txt --modo completo  # roda tudo e mostra o resultado
```

Ela é usada quando se passa `--terminal`, `--modo` ou `--validar`, quando a
entrada vem de um script (não de um teclado) e quando não há servidor gráfico
(SSH, WSL do Windows 10, Linux sem área de trabalho). Nesse último caso o
programa avisa o motivo antes.

Fluxo:

1. informe o caminho do arquivo (qualquer nome, qualquer pasta ou disco);
2. a tela de configuração mostra sistema e tarefas com os valores atuais e
   permite trocar algoritmo, quantum, CPUs, editar/adicionar/remover tarefas ou
   carregar outro arquivo (Enter aceita o valor sugerido entre colchetes);
3. escolha o modo: **(a) passo a passo** ou **(b) execução completa**.

Comandos da tela de configuração:

| Comando | Ação |
|---------|------|
| `Enter` | iniciar (depois pergunta o modo: `a` passo a passo, `b` completa) |
| `a` / `k` / `c` | troca algoritmo / quantum / quantidade de CPUs |
| `e id` / `r id` | edita / remove a tarefa |
| `n` | nova tarefa |
| `l` | carrega outro arquivo |
| `q` | sair |

Ao final da simulação o Gantt completo é salvo em **SVG** (abre em qualquer
navegador) — no modo passo a passo isso acontece automaticamente quando o
relógio chega ao fim, e o comando `x` (ou o botão "Exportar SVG") exporta a
qualquer momento.

### Comandos do modo passo a passo no terminal

| Comando | Ação |
|---------|------|
| `Enter` ou `n [k]` | avança 1 (ou k) tick(s) |
| `p [k]` | volta 1 (ou k) tick(s) |
| `g t` | vai para o instante t |
| `r` | roda até o fim |
| `s` | estado do sistema (todas as tarefas, CPUs, utilização) |
| `t id` | TCB completo de uma tarefa |
| `e id` | edita a tarefa: cor, duração, período, prazo, ingresso, ticks restantes, suspender/retomar, encerrar |
| `+` | adiciona uma tarefa |
| `a` / `k` / `c` | troca algoritmo / quantum / quantidade de CPUs |
| `v t` / `v` | fixa a janela do Gantt em t / volta a seguir o relógio |
| `f` | Gantt completo até o instante atual |
| `x [arquivo]` | exporta SVG |
| `h` / `q` | ajuda / sair |

Editar num instante passado descarta o que tinha sido simulado depois dele (como
num debugger) e a simulação segue a partir do estado editado. Edições que não
fazem sentido são recusadas com o motivo.

### Gantt

- Eixo Y: tarefa de **menor id embaixo**, ids crescentes para cima; abaixo do
  eixo do tempo, uma linha por **CPU** mostra quem a ocupou e quando ficou
  **desligada**.
- Executando: cor da tarefa com o número da CPU. Pronta: sem cor (tracejado).
  Suspensa: preto pontilhado.
- Marcadores: chegada (↓), fim de ativação (↑ / △), término da 10ª ativação (■),
  perda de prazo (✗ vermelho), sorteio de desempate (?).

## Arquivo de configuração

```
algoritmo_escalonamento;quantum;qtde_cpus
id;cor;ingresso;duracao;periodo;prazo;lista_eventos
```

Exemplo (tabela 2.1 do capítulo, figura 2.5):

```
RM;2;1
1;E74C3C;0;20;100;100
2;3498DB;0;40;150;150
3;2ECC71;0;100;350;350;
```

- `RM`/`EDF` sem diferenciar maiúsculas; `;` no fim da linha é opcional; espaços,
  linhas em branco, `\r\n` do Windows e BOM UTF-8 são aceitos; cor com ou sem `#`.
- Campo vazio usa o valor padrão (com aviso). `prazo` vazio ou ausente = período.
- `periodo = 0` (aperiódica) é ignorada com aviso; `periodo < 0` é erro.
- `lista_eventos` é guardada sem interpretação para o Projeto B: o texto cru e a
  lista separada por `,` ou `;` (sem limite de eventos por tarefa; nunca gera erro).
  Ao carregar, um aviso informa quantos eventos cada tarefa tem.
- Erros dizem arquivo, linha, campo e motivo, por exemplo:
  `config.txt:2: campo 'periodo' = -3: valor negativo não é permitido (...)`.

Mais exemplos em `exemplos/` e `tests/validos/`.

## Decisões de implementação

- **Modelo de tempo**: o tick t é o intervalo [t, t+1). Chegadas acontecem no
  instante t; fim de ativação e perda de prazo são verificados no instante t+1.
  A ativação k chega em `ingresso + k·período` e tem deadline
  `chegada + prazo`. Cada tarefa termina após 10 ativações.
- **Escalonamento global**: a cada tick, as M tarefas de maior prioridade
  (M = CPUs) executam; quem já estava numa CPU continua nela (evita migração).
  CPU sem tarefa fica desligada.
- **Desempate** (todos os algoritmos): (1) quem estava executando; (2) deadline
  absoluto mais próximo (urgência real da ativação atual); (3) ingresso da tarefa mais antigo (instante de
  criação, como define o enunciado); (4) menor duração;
  (5) sorteio. O sorteio só acontece quando decide quem executa, usa um gerador
  com estado guardado no snapshot (voltar/avançar é determinístico) e aparece no Gantt.
- **Quantum**: ao fim do quantum o escalonador é chamado de novo (evento
  "fim de quantum", visível no passo a passo); pelo desempate (1) a mesma
  tarefa continua se ainda for a de maior prioridade. Assim o quantum existe
  sem distorcer RM/EDF nem a ordem 1→5 do desempate.
- **Ativação nova antes da anterior terminar** (atraso ou prazo > período): a
  nova fica acumulada e começa assim que a anterior termina.
- **Perda de prazo**: marcada uma vez por ativação; a tarefa continua até
  completar a duração.
- **Suspensa**: no Projeto A não há eventos de suspensão; o estado aparece
  quando o usuário suspende uma tarefa no modo passo a passo.
- **Histórico**: um snapshot completo por tick (simples e garantidamente
  correto). O modo completo não guarda snapshots.
- **Prioridade nominal × ativa**: o algoritmo só informa a prioridade nominal
  (um número, menor = mais prioritária: período no RM, deadline absoluto no
  EDF); o escalonador compara a prioridade ativa, que no Projeto A é igual à
  nominal e no Projeto B receberá a herança de prioridade.
- **Marcadores no Gantt**: cada evento fica na coluna do seu instante exato (a
  coluna t começa no instante t), no terminal e no SVG.
- **Escalonador plugável**: um algoritmo é uma função que devolve a prioridade
  nominal de uma tarefa (`include/sched.h`). Para incluir um novo, crie `src/sched/sched_xxx.c` e registre em
  `src/sched/sched_registro.c`, ou compile como biblioteca dinâmica (`plugins/exemplo_fifo.c`,
  `make plugins`) e carregue com `--plugin`.
- **Interface sem biblioteca**: a janela é desenhada pelo próprio programa
  num vetor de pixels (`src/gfx/tela.c`, com uma fonte 8x16 embutida em
  `src/gfx/fonte.c`), e um arquivo por sistema só abre a janela e copia a
  imagem para ela: no Linux/WSL, `gfx_x11.c` fala o protocolo X11 direto no
  socket do servidor gráfico (sem libX11); no Windows, `gfx_win32.c` usa a API
  Win32; no macOS, `gfx_cocoa.c` usa o Cocoa pelo runtime do Objective-C, em C.
  Nada precisa ser instalado além do compilador. A interface de terminal (cores
  ANSI 24-bit) continua disponível com `--terminal`, e a imagem final é um SVG
  gerado à mão.

## Estrutura

```
projetoSO/
├── src/
│   ├── main.c              # argumentos de linha de comando e fluxo principal
│   ├── core/
│   │   ├── config.c        # leitura e validação do arquivo, valores padrão
│   │   ├── edicao.c        # regras de edição de tarefas (comuns às duas interfaces)
│   │   ├── sim.c           # relógio, ativações, ciclo do tick, eventos
│   │   ├── history.c       # snapshots (avançar/retroceder) e rastro do Gantt
│   │   └── util.c          # strings, números, terminal, gerador aleatório
│   ├── sched/
│   │   ├── sched_rm.c      # Rate Monotonic
│   │   ├── sched_edf.c     # Earliest Deadline First
│   │   ├── sched_tie.c     # cadeia de desempate comum
│   │   └── sched_registro.c  # tabela de escalonadores e carga de plugins
│   ├── gantt/
│   │   ├── gantt_tui.c     # Gantt no terminal
│   │   ├── gantt_gfx.c     # Gantt na janela gráfica
│   │   └── gantt_svg.c     # exportação SVG
│   ├── gfx/
│   │   ├── tela.c          # desenho em memória (retângulos, linhas, texto)
│   │   ├── fonte.c         # fonte bitmap 8x16 embutida
│   │   ├── gfx_x11.c       # janela no Linux/WSL (protocolo X11 direto)
│   │   ├── gfx_win32.c     # janela no Windows (Win32)
│   │   └── gfx_cocoa.c     # janela no macOS (Cocoa)
│   └── ui/
│       ├── ui.c            # interface de terminal: configuração, passo a passo e completo
│       └── ui_gui.c        # a mesma interface na janela gráfica
├── include/                # cabeçalhos (config.h, task.h, sim.h, sched.h, gantt.h, ui.h, util.h, edicao.h, gfx.h, tela.h)
├── plugins/                # escalonador de exemplo carregado com dlopen
├── exemplos/               # figuras 2.5 e 2.6 do capítulo, várias CPUs, quantum
├── tests/                  # arquivos válidos/inválidos e run_tests.sh
├── docs/                   # enunciado do trabalho (PDF)
├── Makefile
└── README.md
```
