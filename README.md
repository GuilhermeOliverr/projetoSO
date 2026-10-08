# projetoSO

Simulador de um sistema operacional multitarefa preemptivo com escalonadores de
tempo real **Rate Monotonic (RM)** e **Earliest Deadline First (EDF)**, de 1 a N
CPUs, desenvolvido em C11 para a disciplina de Sistemas Operacionais (Projeto A).
Só usa a biblioteca padrão do C: não precisa instalar nada além do compilador.

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
./projetoSO                            # pergunta o arquivo e o modo
./projetoSO exemplos/fig2_5_rm.txt     # já carrega o arquivo
make test                              # roda a bateria de testes
make clean                             # remove executável, plugins e SVGs gerados
```

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

(No MSYS2 os plugins `.so` ficam desativados; o resto funciona igual.)

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
```

Fluxo sem `--modo`:

1. informe o caminho do arquivo (qualquer nome, qualquer pasta ou disco);
2. a tela de configuração mostra sistema e tarefas com os valores atuais e
   permite trocar algoritmo, quantum, CPUs, editar/adicionar/remover tarefas ou
   carregar outro arquivo (Enter aceita o valor sugerido entre colchetes);
3. escolha o modo: **(a) passo a passo** ou **(b) execução completa**.

Ao final da simulação o Gantt completo é salvo em **SVG** (abre em qualquer
navegador) — no modo passo a passo isso acontece automaticamente quando o
relógio chega ao fim, e o comando `x` exporta a qualquer momento.

### Comandos do modo passo a passo

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
- `lista_eventos` é guardada crua para o Projeto B.
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
  absoluto mais próximo; (3) ingresso mais antigo; (4) menor duração;
  (5) sorteio. O sorteio só acontece quando decide quem executa, usa um gerador
  com estado guardado no snapshot (voltar/avançar é determinístico) e aparece no Gantt.
- **Quantum**: RM e EDF não usam fatia de tempo para prioridade, então o quantum
  reveza tarefas **empatadas**: ao esgotá-lo a tarefa cede a vez às empatadas
  (round-robin dentro do mesmo nível de prioridade).
- **Ativação nova antes da anterior terminar** (atraso ou prazo > período): a
  nova fica acumulada e começa assim que a anterior termina.
- **Perda de prazo**: marcada uma vez por ativação; a tarefa continua até
  completar a duração.
- **Suspensa**: no Projeto A não há eventos de suspensão; o estado aparece
  quando o usuário suspende uma tarefa no modo passo a passo.
- **Histórico**: um snapshot completo por tick (simples e garantidamente
  correto). O modo completo não guarda snapshots.
- **Escalonador plugável**: um algoritmo é uma função de comparação de
  prioridade (`include/sched.h`). Para incluir um novo, crie `src/sched/sched_xxx.c` e registre em
  `src/sched/sched_registro.c`, ou compile como biblioteca dinâmica (`plugins/exemplo_fifo.c`,
  `make plugins`) e carregue com `--plugin`.
- **Interface**: terminal com cores ANSI 24-bit e imagem final em SVG gerada à
  mão — nenhuma biblioteca gráfica.

## Estrutura

```
projetoSO/
├── src/
│   ├── main.c              # argumentos de linha de comando e fluxo principal
│   ├── core/
│   │   ├── config.c        # leitura e validação do arquivo, valores padrão
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
│   │   └── gantt_svg.c     # exportação SVG
│   └── ui/
│       └── ui.c            # tela de configuração, modo passo a passo e modo completo
├── include/                # cabeçalhos (config.h, task.h, sim.h, sched.h, gantt.h, ui.h, util.h)
├── plugins/                # escalonador de exemplo carregado com dlopen
├── exemplos/               # figuras 2.5 e 2.6 do capítulo, várias CPUs, quantum
├── tests/                  # arquivos válidos/inválidos e run_tests.sh
├── docs/                   # plano do projeto, enunciado e capítulo de referência
├── Makefile
└── README.md
```
