# projetoSO

Simulador de um sistema operacional multitarefa preemptivo com escalonadores de
tempo real **Rate Monotonic (RM)** e **Earliest Deadline First (EDF)**, de 1 a N
CPUs, desenvolvido em C11 para a disciplina de Sistemas Operacionais (Projetos A
e B). O Projeto B acrescenta tarefas **aperiódicas** e **esporádicas**, executadas
pelos servidores **BS, PS, DS e SS**, e o **teste de escalonabilidade** antes de
aceitar cada tarefa (seção "Projeto B").
Tem interface gráfica em janela no Linux/WSL, no Windows e no macOS, feita só
com a biblioteca padrão do C e com o que já vem em cada sistema: não precisa
instalar nada além do compilador.

## Requisitos

- `gcc` (compilador C com suporte a C11)
- `make`(Não é 100% obrigatório, mas é importante ter para que possa compilar facilmente, leia mais logo abaixo)

### Compilando com o `make`

O `make` é uma ferramenta que automatiza a compilação. Ele lê o arquivo
`Makefile` da pasta do projeto, que guarda os comandos do `gcc` com todas as
opções e a lista de arquivos, e executa tudo com uma palavra só. Sem ele você
teria de digitar o comando inteiro do `gcc` a cada compilação. Os alvos do
projeto são:

| Comando | O que faz |
|---------|-----------|
| `make` | compila o simulador e gera o executável `projetoSO` |
| `make plugins` | compila o escalonador de exemplo (`exemplo_fifo.so`) |
| `make test` | compila tudo e roda a bateria de testes (`tests/run_tests.sh`) |
| `make clean` | apaga o executável, os plugins e os SVGs gerados |

Para saber se já está instalado, rode `make --version`. Se não estiver:

- **Ubuntu/Debian e WSL**: `sudo apt install build-essential` (instala o
  `make` junto com o `gcc`; é o mesmo comando da seção Instalação).
- **macOS**: `xcode-select --install` (traz `make` e compilador).
- **Windows com MSYS2**: `pacman -S make`, no terminal *MSYS2 UCRT64*.
- **Fedora**: `sudo dnf install make gcc`.

Com ele instalado, basta entrar na pasta do projeto e rodar `make`.

### Compilando sem o `make`

Se não quiser instalar o `make`, ele não é obrigatório: só chama o compilador. Se ele não estiver instalado, rode o `gcc`
direto, na pasta do projeto:

```bash
# Linux ou WSL
gcc -std=c11 -O2 -o projetoSO config.c gantt.c gfx.c gfx_plataforma.c main.c sched.c sim.c ui.c ui_gui.c -ldl

# macOS
gcc -std=c11 -O2 -o projetoSO config.c gantt.c gfx.c gfx_plataforma.c main.c sched.c sim.c ui.c ui_gui.c -ldl \
    -framework Cocoa -framework CoreGraphics -lobjc
```

```powershell
# Windows (MSYS2); sem -ldl, os plugins ficam desativados
gcc -std=c11 -O2 -o projetoSO.exe config.c gantt.c gfx.c gfx_plataforma.c main.c sched.c sim.c ui.c ui_gui.c -lgdi32
```

Depois é só executar `./projetoSO config.txt` (no Windows,
`.\projetoSO.exe config.txt`), onde `config.txt` é qualquer arquivo de
configuração (formato na seção "Arquivo de configuração"). O plugin de
exemplo, sem o `make`:

```bash
gcc -std=c11 -O2 -fPIC -shared -o exemplo_fifo.so exemplo_fifo.c
```

Todo o código fica numa pasta só, sem subpastas (o formato da entrega no
Moodle, que não guarda pastas). Os arquivos de exemplo ficam em `exemplos/` e a
bateria de testes automáticos em `tests/` (rode com `make test`).

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

O código fica na raiz do repositório, um arquivo por módulo (ver "Estrutura").
Versões antigas organizavam o mesmo código em pastas (`src/`, `include/`,
`plugins/`); elas continuam no histórico do git.

## Compilação e execução

### Linux ou dentro do WSL

```bash
make                                   # compila e gera o executável projetoSO
./projetoSO                            # abre a janela e pede o arquivo
./projetoSO config.txt                 # abre a janela já com o arquivo
./projetoSO --terminal                 # mesma interface, no terminal
make plugins                           # compila o escalonador de exemplo (.so)
./projetoSO config.txt --plugin exemplo_fifo.so
make clean                             # remove executável, plugins e SVGs gerados
```

No WSL do Windows 11 a janela aparece normalmente, porque o WSLg já vem com o
servidor gráfico. Sem servidor gráfico (ex.: WSL do Windows 10, SSH), o
programa avisa e usa o terminal sozinho.

### Windows, pelo PowerShell (usando o WSL)

Dentro da pasta do projeto, sem precisar abrir o Ubuntu:

```powershell
wsl -d Ubuntu make
wsl -d Ubuntu ./projetoSO config.txt
```

### Windows com MSYS2

```powershell
make
.\projetoSO.exe config.txt
```

A janela usa a API Win32, que faz parte do Windows. (No MSYS2 os plugins
`.so` ficam desativados; o resto funciona igual.)

### macOS

Só é preciso o compilador do sistema (`xcode-select --install`, se ainda não
tiver). A janela usa o Cocoa, que já vem no macOS.

```bash
make
./projetoSO config.txt
```

## Como usar

```
./projetoSO [arquivo_config] [opções]

  --modo passo|completo  modo de execução, sem perguntar
  --saida ARQ.svg        onde salvar o Gantt (padrão: <nome do config>_gantt.svg)
  --texto                no modo completo, imprime também a linha do tempo em texto
  --validar              só carrega e valida o arquivo (código de saída 0 = válido)
  --semente N            semente do sorteio de desempate (padrão 1; 0 = relógio)
  --plugin ARQ           carrega um escalonador: ARQ.txt (fórmula), ARQ.so ou ARQ.dylib;
                         pode repetir (ver "Como incluir um novo escalonador")
  --sem-teste            desliga o teste de escalonabilidade (aceita todas as tarefas,
                         para ver sobrecarga e perda de prazo como no Projeto A)
  --sem-cor              desliga as cores ANSI
  --terminal             usa a interface de texto em vez da janela gráfica
  -h, --help             mostra a ajuda
```

Sem `--modo`, o programa abre uma **janela gráfica**:

1. **Abrir arquivo**: digite o caminho (qualquer nome, qualquer pasta ou disco)
   ou clique num arquivo da lista (clique de novo, ou Enter, para carregar).
   Atalhos: **Pendrives e discos** (Linux: `/media/<usuário>`; Windows: lista
   de unidades C:, D:, E:...; macOS: `/Volumes`), **Pasta pessoal**, **Pasta do
   programa** e **Pasta acima**.
   Se o arquivo tiver erros, todos aparecem com linha, campo e motivo.
2. **Configuração**: mostra sistema e tarefas com os valores atuais; botões
   para trocar algoritmo, quantum e CPUs, carregar um escalonador de um arquivo
   (**Carregar escalonador...**), editar/adicionar/remover tarefas ou abrir outro
   arquivo. Os formulários já vêm com os valores sugeridos e
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
./projetoSO config.txt --terminal              # já carrega o arquivo
./projetoSO config.txt --modo passo            # direto no passo a passo
./projetoSO config.txt --modo completo         # roda tudo e mostra o resultado
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
| `e id` / `r id` | edita / remove a tarefa (o id pode ser texto: `e T0`, `r A`) |
| `n` | nova tarefa: escolhe o tipo (periódica, aperiódica, esporádica, servidor ou digitar a linha do `.txt`) |
| `p arquivo` | carrega um escalonador (`.txt` com a fórmula, `.so` ou `.dylib`) e passa a usá-lo |
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
| `t id` | TCB completo de uma tarefa (`t 1`, `t T0`; sem diferenciar maiúsculas) |
| `e id` | edita a tarefa: cor, duração, período, prazo, ingresso, ticks restantes, suspender/retomar, encerrar (no servidor, duração e restante são a capacidade cheia e a que resta) |
| `+` | adiciona uma tarefa (mesmo seletor de tipo do `n`) |
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
- Projeto B: **servidor executando** = cor do servidor com listras diagonais (no
  terminal, `▒`); **aperiódica/esporádica executando** (por um servidor) = cor
  dela com borda grossa tracejada (no terminal, o nº da CPU sublinhado);
  **entrada do servidor** no sistema = losango (◆); **recarga** da capacidade =
  círculo verde com `+`; **entrou no servidor** = triângulo azul (`»` no
  terminal); **capacidade descartada** pelo PS = círculo cinza com `−` (`∅`). A
  linha da CPU mostra a tarefa e o servidor: `C(PS1)`. A legenda ganha esses
  símbolos quando há algum servidor.
- Linha da CPU no Gantt do terminal: cada trecho mostra o id da tarefa que ocupou
  a CPU. Ids de texto aparecem sem o prefixo comum a todos (T1, T2, T3 viram 1, 2,
  3, como com ids inteiros). Num trecho curto demais para o id, aparece o
  caractere que o diferencia dos outros (com A1, A2 e B1: `1`, `2` e `B`).

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
- **id**: número ou texto, como `0`, `1`, `01`, `-1`, `T0`, `t1`, `A`, `Tarefa_2`
  ou `Tarefa 2`, começando em 0 ou em 1, seguidos ou não. Pode ter até 31 bytes
  (letra acentuada ocupa 2) e não pode ser vazio nem ter caractere de controle
  (ex.: tab). Arquivo salvo em ANSI/Latin-1 pelo Bloco de Notas também é aceito:
  o acento aparece como `?` na janela e no SVG.
  - **Ordem** (menor id junto do eixo X): ids inteiros pelo valor
    (`-1 < 0 < 2 < 10`) e antes dos de texto; os de texto em ordem natural, sem
    diferenciar maiúsculas, com os números dentro do texto valendo pelo valor
    (`T0 < T1 < T2 < T10`).
  - **Repetidos** (erro): `T1` e `t1`, `01` e `1`, `T01` e `T1` são o mesmo id.
    `0` e `T0` não são (um é número, o outro texto).
  - **Rótulo** no Gantt: se todos os ids são inteiros, `T<id>` (T0, T1, T10);
    se algum é texto, todos aparecem como foram escritos (evita `TT1`, e que
    `0` e `T0` apareçam os dois como `T0`). Um id inteiro é guardado na forma do
    número: `01` aparece como `1`, igual a quando o id era um `int`.
  - **Tarefa nova**: sugere o maior id + 1 se todos são inteiros (0, 1, 2 → 3);
    o próximo do mesmo prefixo se os de texto são todos prefixo + número
    (T0, T1, T5 → T6); senão n+1, pulando os que já existem.
- **Comentário**: linha que começa com `#` e não tem nenhum `;` é ignorada. Com
  `;` ela é lida normalmente: `#1;E74C3C;0;2;10;10` é a tarefa de id `#1`.
- Campo vazio usa o valor padrão (com aviso). `prazo` vazio ou ausente = período
  (esporádica: o intervalo mínimo; aperiódica: sem prazo).
- `periodo > 0`: periódica; `periodo = 0`: aperiódica; `periodo < 0`: esporádica,
  com `|periodo|` = intervalo mínimo e as ocorrências em `lista_eventos`. Id
  `BSxx`, `PSxx`, `DSxx` ou `SSxx`: servidor (ver "Projeto B").
- `lista_eventos` é guardada sem interpretação: o texto cru e a
  lista separada por `,` ou `;` (sem limite de eventos por tarefa; nunca gera erro).
  Ao carregar, um aviso informa quantos eventos cada tarefa tem. Na esporádica,
  os itens são os instantes das ocorrências e precisam ser inteiros >= 0.
- Erros dizem arquivo, linha, campo e motivo, por exemplo:
  `config.txt:3: campo 'periodo' = 0: o servidor PS é uma tarefa periódica: o período (...) precisa ser maior que zero`.

Mais exemplos nas pastas `exemplos/` e `tests/validos/`.

## Projeto B: aperiódicas, esporádicas e servidores

### Tipos de tarefa (req. 1.2, 1.3 e 2)

O tipo sai do próprio arquivo, sem coluna nova, e fica no TCB (`tipo`, `srv`):

| Linha | Tipo |
|---|---|
| `T1;E74C3C;0;2;10;10` | periódica (`periodo > 0`), como no Projeto A |
| `A;F39C12;5;3;0` | aperiódica (`periodo = 0`, guardado no TCB): chega uma vez, no ingresso; prazo vazio = sem prazo |
| `E;1ABC9C;0;2;-20;20;0,20,45` | esporádica (`periodo < 0`): intervalo mínimo 20; ocorrências em 0, 20 e 45, relativas ao ingresso (com ingresso 10 elas chegariam em 10, 30 e 55) |
| `PS1;2ECC71;0;2;10;10` | servidor: `BSxx`, `PSxx`, `DSxx` ou `SSxx` (sem diferenciar maiúsculas); `duracao` = capacidade, `periodo` = período do servidor |

- **Intervalo mínimo** (req. 1.3, conferido em `config.c` ao ler a linha): se duas
  ocorrências seguidas ficam a menos de `|periodo|` uma da outra (ou fora de
  ordem), a esporádica é **ignorada com aviso** e o resto do arquivo carrega
  normalmente (decisão da equipe, sugerida pelo professor). Uma ocorrência que não
  é inteiro >= 0 é erro de formato, como um `duracao = abc`.
- Servidor PS, DS ou SS precisa de `periodo > 0` (é uma tarefa periódica); o BS
  aceita 0, porque não usa período.

### Como os servidores executam (req. 1.1, 2.1 e 2.2; seção 2.8 do capítulo)

- Aperiódicas e esporádicas **nunca disputam a CPU sozinhas**: quando chegam, vão
  para uma **fila global FIFO** (pela chegada da ativação; empate pelo id).
- O servidor é uma tarefa periódica comum para o escalonador: prioridade do
  algoritmo (RM: período; EDF: chegada do período + prazo; no SS, a chegada é o
  início do intervalo ativo) e o mesmo desempate do req. 4.3. Ele só disputa a CPU
  quando **tem capacidade e tem o que executar**.
- O servidor que ganha a CPU tira a primeira tarefa da fila e fica com ela: ela
  fica **inacessível para os outros servidores** (req. 2.2), mesmo enquanto ele é
  preemptado. Ele a larga quando ela termina a ativação ou quando a **capacidade
  dele acaba**: aí ela volta para a fila, no lugar dela, e outro servidor pode
  continuá-la (ex.: `exemplos/servidores_varios.txt`, W começa no PS1 e termina
  no DS1).
- Cada tick de execução gasta 1 da capacidade do servidor e 1 do restante da
  tarefa.

| Servidor | Capacidade |
|---|---|
| **BS** (Background) | não tem: só pega uma CPU que sobrou depois de todas as periódicas e dos outros servidores, então nunca atrasa ninguém |
| **PS** (Polling) | volta cheia no início de cada período; se não há tarefa pendente, o PS **perde** o que sobrou até o próximo período (figura 2.15) |
| **DS** (Deferrable) | volta cheia no início de cada período e é **guardada** enquanto não há pendência (figura 2.16) |
| **SS** (Sporadic) | volta só o que foi consumido, no instante **RT = início do intervalo ativo + período** (figura 2.17). O intervalo ativo começa quando o SS fica pronto (tem capacidade e há tarefa para ele) e termina quando ele deixa de estar pronto |

Os servidores terminam junto com a última tarefa que não é servidor. Conferido
contra as figuras 2.15 a 2.17 com os tempos multiplicados por 2 (o simulador usa
ticks inteiros): `exemplos/servidor_ps.txt` e `exemplos/servidor_ss.txt`.

### Teste de escalonabilidade (req. 3)

Antes de aceitar qualquer tarefa (na carga do arquivo e ao adicionar pela
interface), ela é testada junto com as já aceitas. Se não passa, aparece o aviso

```
tarefa 2 impedida de ser escalonada: com ela a utilização seria U = 1.000, acima do limite 0.828
(RM, Liu & Layland: n(2^(1/n)-1) com n = 2), e a análise de tempo de resposta (seção 2.5 do
capítulo) confirma: ela teria tempo de resposta 55 > 50 no pior caso; algoritmo RM
```

e o resto carrega normalmente. Na carga, periódicas e servidores são testados
primeiro e depois aperiódicas e esporádicas (que dependem dos servidores).

- **U** = soma de C/P das periódicas e dos servidores PS, DS e SS. O BS não conta
  (só usa tempo livre); aperiódica e esporádica também não (quem gasta CPU por elas
  é o servidor).
- **RM**: limite de Liu & Layland `n(2^(1/n)-1)`, com PS e SS como tarefas
  periódicas (o capítulo diz isso dos dois). Com DS, a equação [19] do capítulo na
  forma para n tarefas: `U_P <= n(((U_DS+2)/(2U_DS+1))^(1/n) - 1)`. Esse limite é só
  suficiente; quando U passa dele com 1 CPU, vale a **análise de tempo de
  resposta** da seção 2.5 (exata para prazo <= período), com o DS entrando com
  jitter P - C (ele pode executar no fim de um período e no começo do seguinte).
  Por isso a figura 2.17 (U = 0,879) é aceita e a figura 2.6 com RM é recusada (B
  teria R = 55 > 50, a perda que o livro mostra).
- **EDF**: U <= 1. **Outros algoritmos** (fórmula ou plugin): só a condição
  necessária U <= 1.
- **M CPUs**: o limite de 1 CPU vezes M. O capítulo só tem testes para 1 CPU; é a
  extensão mais simples, mas com fila global é uma aproximação (efeito Dhall), por
  isso o Gantt continua mostrando qualquer perda de prazo.
- **Aperiódica/esporádica**: precisa de pelo menos um servidor (sem servidor ela
  esperaria para sempre). A soma de C/intervalo mínimo das esporádicas tem de
  caber no que os servidores oferecem (soma de C/P dos PS, DS e SS, mais o tempo
  livre se houver BS).
- `--sem-teste` desliga o teste (menos a exigência de servidor), para mostrar
  sobrecarga e perda de prazo como no Projeto A. As edições durante a simulação
  não passam pelo teste: o debugger pode criar sobrecarga de propósito.

### Inserção manual (terminal e janela)

`n` (configuração), `+` (passo a passo) ou o botão "+ Nova tarefa" abrem um
**seletor de tipo**: periódica, aperiódica, esporádica, servidor BS/PS/DS/SS ou
**digitar a linha do `.txt`**. O formulário traz os campos do tipo: na aperiódica
o período é 0 e o prazo pode ficar vazio; na esporádica o período vira o
**intervalo mínimo** e os eventos viram as **ocorrências**, várias de uma vez
(`0,15,40`); no servidor, a duração é a capacidade. Os campos viram **a mesma
linha de um arquivo `.txt`** (mostrada ao usuário) e ela é lida pelas mesmas
regras do arquivo (`config_ler_linha_manual`, que usa a `ler_linha_tarefa` do
arquivo), inclusive o intervalo mínimo, e depois passa pelo teste de
escalonabilidade.

### Exemplos e testes

| Arquivo | O que mostra |
|---|---|
| `exemplos/servidor_bs.txt` | BS usando só a CPU livre, em ordem de chegada |
| `exemplos/servidor_ps.txt` | figura 2.15: o PS perde a capacidade e D espera o próximo período |
| `exemplos/servidor_ds.txt` | o DS guarda a capacidade e atende D na hora (a carga da figura 2.16 não passa na eq. [19], então foi reduzida) |
| `exemplos/servidor_ss.txt` | figura 2.17: recargas em RT = 29 e 36 |
| `exemplos/servidores_varios.txt` | PS, DS, SS e BS na mesma fila; uma tarefa passa de um servidor a outro |

Todos passam no teste de escalonabilidade e são conferidos por `make test`.

## Como incluir um novo escalonador

O contrato (req. 4.2): um escalonador só diz a **prioridade nominal** de uma
tarefa num instante (um número; menor = mais prioritária). A simulação monta a
fila global, aplica o desempate comum (CPU anterior, prazo, ingresso, duração,
sorteio) e entrega as M melhores às M CPUs. É um número, e não "a próxima
tarefa", porque com M CPUs é preciso ordenar todas e pegar as M melhores, com o
mesmo desempate para qualquer algoritmo. A simulação nunca cita RM, EDF ou
fórmula.

| Forma | Como | Quando usar |
|---|---|---|
| Nativo | uma função de prioridade e um `const Escalonador` em `sched.c` (seção sched_registro.c), mais uma linha na lista `nativos`; recompilar | o que faz parte do programa (RM, EDF) |
| Plugin `.so`/`.dylib` | compilar como biblioteca (`exemplo_fifo.c`, `make plugins`) e `--plugin arq.so` | algoritmos que uma fórmula não expressa; precisa de compilador e só abre no mesmo sistema e processador em que foi compilado (um `.so` de Linux x86-64 não abre num Mac arm64) |
| Arquivo `.txt` | a fórmula da prioridade, interpretada pelo programa | o mais fácil e portátil: sem compilador, igual no macOS, Linux e Windows, e sem executar código de fora |

### O arquivo `.txt`

```
# LLF.txt - Least Laxity First
nome: LLF
descricao: menor folga primeiro
prioridade: deadline_abs - agora - restante
```

O arquivo está em `exemplos/LLF.txt`, e `exemplos/llf_auto.txt` é uma
configuração `LLF;2;1` que o carrega automaticamente (ver "Como carregar").
- Uma chave por linha, `chave: valor` ou `chave = valor`. Chaves (sem diferenciar
  maiúsculas): `nome` e `prioridade` (obrigatórias) e `descricao` ou `descrição`
  (opcional; padrão "carregado de LLF.txt"). Linha vazia e linha começando com
  `#` são ignoradas; BOM UTF-8 e `\r\n` são aceitos, como no arquivo de
  configuração.
- `nome`: até 31 caracteres, sem espaço nem `;` (ele vai na 1ª linha da
  configuração, ex.: `LLF;2;1`). Não pode repetir um que já existe: um arquivo
  nunca substitui RM/EDF.
- Fórmula: inteiros de 0 a 2147483647, `+ - * / %`, menos unário, parênteses,
  `min(a, b, ...)`, `max(a, b, ...)` e `abs(a)`. Campos e funções sem diferenciar
  maiúsculas.
- Campos (da tarefa avaliada): `periodo`, `prazo`, `duracao`, `ingresso`,
  `restante` (ticks que faltam na ativação atual), `chegada_atual` (instante em
  que a ativação atual chegou), `deadline_abs` (`chegada_atual + prazo`),
  `fila_desde` (instante em que a tarefa entrou no fim da fila de prontas; ver
  "Quantum") e `agora` (o tick que está sendo escalonado).
- A divisão é a do C: inteira, truncando para zero (`7 / 2 = 3`, `-7 / 2 = -3`);
  `%` tem o sinal do dividendo (`-7 % 2 = -1`).
- As contas são feitas em 64 bits, conferindo estouro em cada operação; o
  resultado precisa caber num `int`.
- A fórmula é conferida e compilada (para notação pós-fixa) na carga; a cada tick
  ela só é avaliada, usando apenas a própria tarefa e `agora`. Por isso voltar no
  tempo e avançar de novo dá o mesmo resultado.

| Algoritmo | `prioridade:` |
|---|---|
| RM (Rate Monotonic) | `periodo` |
| EDF (Earliest Deadline First) | `deadline_abs` |
| DM (Deadline Monotonic) | `prazo` |
| SRTF (menor tempo restante) | `restante` |
| FIFO (chegada da ativação) | `chegada_atual` |
| LLF (menor folga) | `deadline_abs - agora - restante` |
| RR (Round Robin, usa o quantum) | `fila_desde` |

### Como carregar

- Linha de comando: `./projetoSO config.txt --plugin LLF.txt` (aceita `.txt`,
  `.so` e `.dylib`; pode repetir).
- Janela: botão **Carregar escalonador...** na tela de configuração ou na janela
  "Escolha o algoritmo". O navegador abre na pasta do arquivo de configuração, com
  o atalho **Pendrives e discos**; o carregado já fica escolhido.
- Terminal: `p arquivo` na tela de configuração, ou digitar o caminho do
  arquivo quando o comando `a` pede o algoritmo (também no passo a passo).
- Automático (pendrive): se a 1ª linha do arquivo de configuração pede, por
  exemplo, `LLF;2;1` e LLF não está registrado, o programa procura na mesma pasta
  `LLF.txt`, `LLF.so` ou `LLF.dylib`, nessa ordem e sem diferenciar maiúsculas
  (`Llf.TXT` serve), e avisa "escalonador LLF carregado automaticamente de ...".
  O `nome:` de dentro do arquivo precisa ser o mesmo. Arquivos ocultos (inclusive
  os `._*` que o macOS cria em pendrive FAT) são ignorados.

Os carregados entram sempre depois de RM (0) e EDF (1) e não são descarregados
enquanto o programa roda, porque a configuração e o histórico guardam o índice
do algoritmo.

### Erros

- Na carga, todos os problemas do arquivo aparecem de uma vez, com linha e
  coluna, por exemplo `LLF.txt:4: fórmula da prioridade: campo desconhecido
  'periodoo' na coluna 1; os campos são: periodo, prazo, ...`. Se houver erro,
  nada é registrado.
- Durante a simulação, divisão ou resto por zero, estouro ou um resultado fora do
  `int` param a simulação naquele tick, com a mensagem (ex.: `escalonador LLF:
  divisão por zero (10 / 0) ao calcular a prioridade da tarefa 2 no instante 7`).
  Nenhum valor é inventado, porque isso mudaria o escalonamento sem aviso. O que
  foi simulado até o tick anterior continua valendo: dá para voltar no tempo ou
  trocar o algoritmo e seguir.

### Limite

Só cabem algoritmos cuja prioridade é um número calculado a partir da própria
tarefa e do instante. O Round Robin cabe porque a posição na fila virou um campo
da tarefa (`fila_desde`, mantido pela simulação; ver "Quantum"). Filas multinível
e loteria continuam não cabendo: precisam de um estado próprio do escalonador (em
que fila cada tarefa está e quando ela desce de fila; os bilhetes e o sorteio
entre eles) que não é um campo da tarefa, e a fórmula não guarda nada de um tick
para o outro.

## Desempate (req. 4.3)

Quando duas tarefas têm a mesma prioridade, vale o primeiro critério que as
diferencia, nesta ordem, para TODOS os algoritmos (RM, EDF e os carregados por
`.txt` ou `.so`):

1. a tarefa que estava executando logo antes do escalonamento (evita trocar de
   contexto para a própria tarefa);
2. o menor **prazo relativo** da tarefa (o campo `prazo` do arquivo);
3. o menor instante de **ingresso** da tarefa (campo `ingresso`: quando ela foi
   criada, como define o enunciado);
4. a menor **duração**;
5. sorteio, com o "?" no Gantt no momento em que a escolhida (re)começa a
   executar. O gerador guarda o estado no histórico, então voltar e avançar dá
   o mesmo sorteio.

Por que o critério 2 é o prazo relativo, e não o deadline absoluto: é a leitura
literal do enunciado ("o prazo da tarefa", que é o parâmetro `prazo` do item
3.3); no EDF a prioridade já é o deadline absoluto, então desempatar por ele de
novo nunca teria efeito; no RM, entre tarefas de mesmo período, vai primeiro a
de prazo mais restrito. O critério 1 só existe se a tarefa estava na CPU: uma
tarefa preemptada não tem vantagem.

## Quantum

O quantum é lido do arquivo e o simulador conta quantos ticks seguidos cada
tarefa executou. O uso dele é **específico de cada escalonador** (resposta do
professor): RM e EDF decidem só pela prioridade, então ao fim do quantum o
escalonador é chamado de novo (evento "fim de quantum" no painel do tick) e,
pelo critério 1, a mesma tarefa continua se ainda for a mais prioritária. Isso
mantém a ordem 1→5 do desempate igual para todos os algoritmos.

Um escalonador que queira usar o quantum faz isso pela **prioridade**, com o
campo `fila_desde` do TCB: o instante em que a tarefa entrou no fim da fila de
prontas. A simulação o atualiza assim:
- recebe o instante atual quando uma ativação começa a disputar a CPU (na
  chegada; ou, se ela estava acumulada atrás da anterior, quando a anterior
  termina);
- recebe o instante atual quando a tarefa esgota o quantum (volta para o fim
  da fila);
- não muda quando a tarefa é preemptada por outra mais prioritária (ela mantém
  o lugar na fila, como no Round Robin com prioridades).

Round Robin, então, é só uma fórmula:

```
nome: RR
descricao: Round Robin (usa o quantum)
prioridade: fila_desde
```

Exemplo (`RR;2;1`, T1 e T2 com duração 3, T3 com duração 2, todas chegando em
0): `[0,2)T1 [2,4)T2 [4,6)T3 [6,7)T1 [7,8)T2`. Empates de `fila_desde` caem
na mesma cadeia do 4.3 (ex.: as três chegam juntas, e T1 vai primeiro pelo
prazo). Como a vantagem de quem esgotou o quantum some pela prioridade, e não
por uma exceção no desempate, o 4.3 continua valendo sem mudança. RM e EDF não
leem `fila_desde`: a escala deles é a mesma de antes. Plugins `.so` enxergam o
campo pelo TCB e precisam ser recompilados (`make plugins`), porque o TCB mudou
de tamanho.

## Decisões de implementação

- **Id em texto**: o enunciado não fixa o tipo do id, então ele é texto, num
  vetor de tamanho fixo (`char id[ID_MAX]`, 32 bytes, em `sched.h`) dentro do
  TCB e da `TarefaCfg`. Não é `char *` porque o TCB é copiado por `memcpy` a
  cada snapshot do histórico: com ponteiro, cada cópia precisaria de
  `strdup`/`free`, com risco de double free e vazamento. As regras (validar,
  comparar, rótulo, sugestão) ficam todas em `config.c` (funções `id_*`). O
  escalonador não usa o id: o desempate é pela CPU anterior, prazo, ingresso,
  duração e sorteio. As buscas por id devolvem o índice da tarefa (-1 se não
  existe), nunca um id. Plugins `.so` precisam ser recompilados
  (`make plugins`), porque o TCB mudou de tamanho.
- **Modelo de tempo**: o tick t é o intervalo [t, t+1). Chegadas acontecem no
  instante t; fim de ativação e perda de prazo são verificados no instante t+1.
  A ativação k chega em `ingresso + k·período` e tem deadline
  `chegada + prazo`. Cada tarefa termina após 10 ativações.
- **Escalonamento global**: a cada tick, as M tarefas de maior prioridade
  (M = CPUs) executam; quem já estava numa CPU continua nela (evita migração).
  CPU sem tarefa fica desligada.
- **Desempate** (todos os algoritmos): (1) quem estava executando; (2) menor
  prazo relativo (campo `prazo`); (3) ingresso mais antigo; (4) menor duração;
  (5) sorteio. O sorteio só acontece quando decide quem executa. Detalhes e
  justificativa na seção "Desempate".
- **Quantum**: específico de cada escalonador. RM e EDF decidem só pela
  prioridade; quem quiser usar o quantum usa o campo `fila_desde` na prioridade
  (Round Robin = `prioridade: fila_desde`). Ver a seção "Quantum".
- **Ativação nova antes da anterior terminar** (atraso ou prazo > período): a
  nova fica acumulada e começa assim que a anterior termina.
- **Perda de prazo**: marcada uma vez por ativação; a tarefa continua até
  completar a duração.
- **Suspensa**: não há eventos de suspensão; o estado aparece
  quando o usuário suspende uma tarefa no modo passo a passo.
- **Histórico**: um snapshot completo por tick (simples e garantidamente
  correto). O modo completo não guarda snapshots.
- **Prioridade nominal × ativa**: o algoritmo só informa a prioridade nominal
  (um número, menor = mais prioritária: período no RM, deadline absoluto no
  EDF); o escalonador compara a prioridade ativa, que hoje é igual à
  nominal (ficam separadas para a ativa poder mudar sem alterar a nominal).
- **Marcadores no Gantt**: cada evento fica na coluna do seu instante exato (a
  coluna t começa no instante t), no terminal e no SVG.
- **Escalonador plugável**: um algoritmo é uma função que devolve a prioridade
  nominal de uma tarefa (`sched.h`), e há três formas de incluir um novo: nativo,
  plugin `.so`/`.dylib` ou arquivo `.txt` com a fórmula (seção "Como incluir um
  novo escalonador"). A fórmula é compilada na carga e avaliada sem estado
  próprio; uma falha (divisão por zero, estouro) para a simulação no tick, sem
  inventar valor.
- **Projeto B** (detalhes na seção "Projeto B"): o tipo da tarefa sai do período e
  do id; aperiódicas e esporádicas só executam por servidores, numa fila FIFO; a
  tarefa fica com o servidor que a pegou até terminar ou até a capacidade dele
  acabar; a esporádica que viola o intervalo mínimo é ignorada com aviso; o teste
  de escalonabilidade roda antes de aceitar cada tarefa; as perdas de prazo de
  aperiódicas/esporádicas são marcadas com um contador (e não com um bit por
  ativação) porque uma esporádica pode ter mais de 32 ocorrências.
- **Interface sem biblioteca**: a janela é desenhada pelo próprio programa
  num vetor de pixels (`gfx.c`, com uma fonte 8x16 embutida), e um trecho
  de `gfx_plataforma.c` por sistema só abre a janela e copia a
  imagem para ela: no Linux/WSL, a seção `gfx_x11.c` fala o protocolo X11 direto no
  socket do servidor gráfico (sem libX11); no Windows, a seção `gfx_win32.c` usa a API
  Win32; no macOS, a seção `gfx_cocoa.c` usa o Cocoa pelo runtime do Objective-C, em C.
  Nada precisa ser instalado além do compilador. A interface de terminal (cores
  ANSI 24-bit) continua disponível com `--terminal`, e a imagem final é um SVG
  gerado à mão.

## Estrutura

Todo o código fica na mesma pasta. Cada `.c`/`.h` junta os arquivos de um
mesmo módulo da antiga versão em pastas (que está no histórico do git); dentro
dele, cada trecho começa com um divisor que diz de qual arquivo original veio.

```
projetoSO/
├── main.c              # argumentos de linha de comando e fluxo principal
├── config.h / config.c # arquivo de configuração e valores padrão;
│                       #   funções auxiliares (antigo util.c/util.h)
├── sim.h / sim.c       # relógio, ativações, ciclo do tick, eventos;
│                       #   histórico (antigo history.c) e regras de edição (antigo edicao.c/edicao.h)
├── sched.h / sched.c   # TCB (antigo task.h); RM, EDF, desempate comum e
│                       #   tabela de escalonadores (antigos sched_rm/edf/tie/registro.c)
├── gantt.h / gantt.c   # Gantt em SVG, no terminal e na janela (antigos gantt_svg/tui/gfx.c)
├── gfx.h / gfx.c       # desenho em memória e fonte 8x16 embutida (antigos tela.c/tela.h e fonte.c)
├── gfx_plataforma.c    # janela de cada sistema: X11, Win32 e Cocoa (antigos gfx_x11/win32/cocoa.c)
├── ui.h / ui.c         # interface de terminal: configuração, passo a passo e completo
├── ui_gui.c            # a mesma interface na janela gráfica
├── exemplo_fifo.c      # escalonador de exemplo, compilado à parte como biblioteca dinâmica (make plugins)
├── exemplos/           # arquivos das figuras do capítulo 2, um por servidor (Projeto B) e o escalonador LLF.txt
├── tests/              # bateria de testes (make test): validos/, invalidos/ e run_tests.sh
├── docs/               # enunciado do projeto (PDF)
├── Makefile
└── README.md
```
