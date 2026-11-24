# projetoSO

Simulador de um sistema operacional multitarefa preemptivo com escalonadores de
tempo real **Rate Monotonic (RM)** e **Earliest Deadline First (EDF)**, de 1 a N
CPUs, desenvolvido em C11 para a disciplina de Sistemas Operacionais. O Projeto A
simula tarefas periódicas; o Projeto B acrescenta tarefas aperiódicas e
esporádicas, executadas pelos servidores Background, Polling, Deferrable e
Sporadic, e o teste de escalonabilidade na admissão de cada tarefa (seção
"Projeto B").
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
  --sem-admissao         não faz o teste de escalonabilidade ao criar cada tarefa
                         (toda tarefa entra; a aperiódica sem servidor continua recusada)
  --plugin ARQ           carrega um escalonador: ARQ.txt (fórmula), ARQ.so ou ARQ.dylib;
                         pode repetir (ver "Como incluir um novo escalonador")
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
| `n` | nova tarefa |
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
| `s` | estado do sistema (todas as tarefas com o tipo, CPUs, utilização, fila de aperiódicas, resumo do teste de admissão) |
| `t id` | TCB completo de uma tarefa (`t 1`, `t T0`; sem diferenciar maiúsculas); no servidor, a capacidade e a aperiódica que executa; na aperiódica, as ocorrências e o lugar na fila; e a decisão do teste de admissão |
| `d` | decisões do teste de admissão, com o motivo de cada uma |
| `e id` | edita a tarefa: cor, duração (capacidade, no servidor), período, prazo, ingresso, ticks restantes (capacidade atual, no servidor), suspender/retomar, encerrar |
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
- Projeto B: o **servidor** executando aparece com **listras e borda grossa**
  (no terminal, `/`), e a **aperiódica/esporádica** executando com **borda
  tracejada grossa** (no terminal, o número da CPU sublinhado). Na linha da
  CPU, `PS1:A1` é o servidor PS1 executando a aperiódica A1. Marcadores novos:
  recusada no teste de admissão (círculo vermelho cortado, `⊘`), recarga do
  Sporadic Server (círculo verde com +, `⊕`) e capacidade descartada pelo
  Polling Server (círculo laranja com -, `⊖`).
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
  (na esporádica, |período|; na aperiódica, sem prazo).
- O **período diz o tipo** da tarefa: `> 0` periódica, `0` aperiódica e `< 0`
  esporádica; um id `BSxx`, `PSxx`, `DSxx` ou `SSxx` é um servidor (seção
  "Projeto B").
- `lista_eventos`: na aperiódica e na esporádica são os **instantes de
  chegada**, relativos ao ingresso (`0,20,45`), validados ao carregar (inteiros
  >= 0, em ordem crescente e, na esporádica, separados por pelo menos
  |período|). Nas periódicas e nos servidores é guardada sem interpretação: o
  texto cru e a lista separada por `,` ou `;` (sem limite de eventos por
  tarefa), e um aviso informa quantos eventos cada tarefa tem.
- Erros dizem arquivo, linha, campo e motivo, por exemplo:
  `config.txt:4: campo 'lista_eventos' da esporádica E1: as ocorrências 40 e 60
  estão a 20 tick(s) uma da outra, menos que o intervalo mínimo da esporádica
  (|periodo| = 30)`.

Mais exemplos nas pastas `exemplos/` e `tests/validos/`.

## Projeto B

### Tipos de tarefa

| Tipo | No arquivo | Ativações |
|------|------------|-----------|
| periódica | `periodo > 0` | uma por período; termina após 10 (req. 4.4 do Projeto A) |
| aperiódica | `periodo = 0` (fica guardado no TCB) | nos instantes de `lista_eventos`, relativos ao ingresso; sem lista, uma só, no ingresso. Prazo 0 ou vazio = sem prazo |
| esporádica | `periodo < 0`, com \|período\| = intervalo mínimo | nos instantes de `lista_eventos` (`0,20,45`, relativos ao ingresso); a carga recusa ocorrências mais próximas que \|período\| |
| servidor | id `BSxx`, `PSxx`, `DSxx` ou `SSxx` (sem diferenciar maiúsculas; `xx` = número) | `duracao` = capacidade, `periodo` = período do servidor, `prazo` = prazo de cada período |

Exemplo:

```
RM;2;1
1;E74C3C;0;1;4;4
2;3498DB;0;2;8;8
PS1;95A5A6;0;1;5;5
A1;F39C12;2;2;0
E1;2ECC71;3;1;-10;10;0,12
```

### Servidores e fila de aperiódicas

Aperiódicas e esporádicas **nunca disputam a CPU** (req. B 1.1): ficam numa
fila, e quem disputa é o servidor, priorizado pelo próprio RM/EDF como uma
periódica (no RM pelo período; no EDF pelo prazo do período atual). O servidor
que ganha uma CPU tira a primeira da fila e fica com ela até a ativação acabar
ou a capacidade dele esgotar; nenhum outro servidor a vê (req. B 2.2).

- **Fila**: as esporádicas primeiro (o teste de admissão garantiu o prazo
  delas), depois as aperiódicas, cada grupo por ordem de chegada. Uma
  esporádica que chega tira a aperiódica de um servidor (ela volta para a fila).
- **BS** (Background Server): só usa CPU que ficaria desligada; sem capacidade.
- **PS** (Polling Server): a cada período volta a ter a capacidade inteira.
  Disputa a CPU mesmo com a fila vazia; se ganhar a CPU e não houver aperiódica,
  **perde a capacidade** até o próximo período (evento "capacidade descartada"),
  e a CPU vai para a próxima candidata no mesmo tick.
- **DS** (Deferrable Server): recarrega a cada período, mas só disputa a CPU
  com capacidade **e** aperiódica para executar: guarda a capacidade e atende
  a aperiódica assim que ela chega.
- **SS** (Sporadic Server): começa com a capacidade inteira; fica ativo quando
  tem capacidade e aperiódica, e o que gastar volta **um período depois do
  instante em que ficou ativo** (evento "recarga"). No EDF, o prazo dele é esse
  instante mais o prazo.

Os servidores não terminam pela contagem de ativações: acabam junto com a
simulação, quando todas as outras tarefas terminam.

### Teste de admissão (req. B 3)

Cada tarefa passa pelo teste **no instante em que é criada** (o ingresso),
inclusive as adicionadas pela interface durante a simulação. No mesmo
instante entram primeiro as periódicas e os servidores, depois as esporádicas e
por fim as aperiódicas, cada grupo na ordem do arquivo. Recusada, a tarefa
termina ali, sem executar. A decisão e o motivo aparecem no fim do modo
completo, no comando `d`, no `t id` e nos painéis da janela.

| Quem | Teste |
|------|-------|
| periódicas e servidores PS, DS e SS, 1 CPU, RM | Liu & Layland (U <= n(2^(1/n)-1), com prazo = período e sem DS) e, se não bastar, a análise do tempo de resposta. O DS entra com jitter T - C (efeito back-to-back) |
| idem, 1 CPU, EDF | U <= 1 (com prazo >= período e sem DS) ou a análise de demanda do processador, h(L) <= L |
| idem, M CPUs | U <= M é a condição necessária; suficientes: GFB (EDF) e Bertogna, Cirinei e Lipari (RM), com C/min(D,T) |
| Background Server | não entra na carga (só usa CPU ociosa) |
| esporádica | a banda pedida, soma de C/min(D,\|P\|), cabe na banda dos servidores, soma de Cs/Ts, e o pior tempo de resposta na fila cabe no prazo |
| aperiódica | precisa existir servidor. Com prazo, cada ativação é testada ao entrar na fila: o trabalho na frente dela, mais o dela, mais o que as esporádicas podem pedir até o prazo, cabe no que os servidores garantem até lá |

"O que os servidores garantem" num intervalo é o modelo de recurso periódico:
um servidor de capacidade Cs a cada Ts pode ficar até 2(Ts - Cs) sem entregar
nada e depois entrega Cs a cada Ts.

Quando a condição necessária passa mas nenhum teste suficiente fecha (várias
CPUs, Deferrable Server em várias CPUs, algoritmo que não é RM nem EDF, ou
esporádica só com Background Server), a tarefa entra **sem garantia**, com
aviso. Editar uma tarefa que já está no sistema não é bloqueado pelo teste,
mas a mensagem avisa o que ele diria com os valores novos.

Com `--sem-admissao` toda tarefa entra; a aperiódica sem nenhum servidor
continua recusada, porque nunca executaria. Alguns exemplos do Projeto A
mudam com o teste ligado (a fig. 2.6 com RM tem U = 100% e B é recusada, com
R = 55 > 50); para ver a perda de prazo da figura, use `--sem-admissao`.

### Exemplos

Os arquivos `exemplos/b_*.txt` têm a conta à mão nos comentários, conferida
pela bateria de testes:

| Arquivo | Mostra |
|---------|--------|
| `b_background_server.txt` | BS nos ticks ociosos; esporádica antes da aperiódica |
| `b_polling_server.txt` | descarte da capacidade e capacidade acabando no meio da aperiódica |
| `b_deferrable_server.txt` | DS guardando a capacidade e atendendo na chegada |
| `b_sporadic_server.txt` | recargas do SS (9, 12, 17 e 20) com EDF |
| `b_admissao_recusa.txt` | PS1 recusado (R = 8 > 6) e aperiódicas sem servidor |
| `b_aperiodica_com_prazo.txt` | ativação recusada ao entrar na fila e outra aceita |

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
  `chegada + prazo`. Cada tarefa periódica termina após 10 ativações; a
  aperiódica e a esporádica, após a última ocorrência da lista.
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
- **TCB copiável com `memcpy`** também no Projeto B: as ocorrências da
  aperiódica apontam para a configuração (imutável, como os eventos), o
  vínculo servidor-aperiódica é feito por índices, e as recargas pendentes do
  Sporadic Server ficam num vetor fixo (cada trecho ativo gasta ao menos 1
  tick; se o vetor encher, a recarga nova é somada à última e adiada, o que
  nunca deixa o servidor passar da capacidade).
- **Servidor sem trabalho não ocupa CPU**: se um servidor escolhido fica sem
  aperiódica para pegar, a seleção do tick é refeita sem ele (com o sorteio
  voltando ao estado anterior), para a CPU ir para a próxima candidata.
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
├── sim.h / sim.c       # relógio, ativações, ciclo do tick, eventos, servidores e fila de aperiódicas;
│                       #   histórico (antigo history.c) e regras de edição (antigo edicao.c/edicao.h)
├── admissao.h / admissao.c  # teste de escalonabilidade na admissão de cada tarefa (Projeto B)
├── sched.h / sched.c   # TCB (antigo task.h); RM, EDF, desempate comum e
│                       #   tabela de escalonadores (antigos sched_rm/edf/tie/registro.c)
├── gantt.h / gantt.c   # Gantt em SVG, no terminal e na janela (antigos gantt_svg/tui/gfx.c)
├── gfx.h / gfx.c       # desenho em memória e fonte 8x16 embutida (antigos tela.c/tela.h e fonte.c)
├── gfx_plataforma.c    # janela de cada sistema: X11, Win32 e Cocoa (antigos gfx_x11/win32/cocoa.c)
├── ui.h / ui.c         # interface de terminal: configuração, passo a passo e completo
├── ui_gui.c            # a mesma interface na janela gráfica
├── exemplo_fifo.c      # escalonador de exemplo, compilado à parte como biblioteca dinâmica (make plugins)
├── exemplos/           # figuras do capítulo 2, o escalonador LLF.txt e os exemplos do Projeto B (b_*.txt)
├── tests/              # bateria de testes (make test): validos/, invalidos/ e run_tests.sh
├── docs/               # enunciado do projeto (PDF)
├── Makefile
└── README.md
```
