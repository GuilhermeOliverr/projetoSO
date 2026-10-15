# Verificação dos requisitos contra o `steering.md`

Data: 2026-10-09. Base: `docs/steering.md` comparado com o código em `src/` e `include/`.

## Resumo

Todos os requisitos obrigatórios do steering (G1–G6, A1–A5) e os preparativos do §11 estão
cumpridos. Do checklist do §12, o que falta não depende do código: commitar as mudanças e
confirmar as ambiguidades com o professor (ver o fim deste arquivo).

## Como foi verificado

- Leitura de todo o código contra cada requisito do steering (G1–G6, A1–A5, §5–§12).
- `make test`: **55 de 55 testes passam**, com o plugin compilado (`make plugins`).
- O `Makefile` agora compila com `-Wall -Wextra -pedantic -std=c11 -O2 -g`: **sem warnings**.
- **valgrind** (`--leak-check=full --show-leak-kinds=all`): **0 erros e 0 bytes vazados** nos
  modos completo e passo a passo (avançar, voltar, adicionar e editar tarefa, suspender, rodar até
  o fim), no menu de configuração (editar e remover tarefa, CPUs inválidas) e com plugin. Usei um
  valgrind extraído do pacote do Ubuntu, sem instalar no sistema; no WSL basta
  `sudo apt install valgrind`.
- Os mesmos fluxos rodaram com AddressSanitizer e UBSan, também sem erros.

## Requisitos

| Grupo | Situação |
|---|---|
| G1–G6 | Ok: preemptivo, de 1 a 64 CPUs, Gantt com legenda na janela gráfica, no terminal e em SVG, avançar/retroceder, só libc (+ `dlopen`) e o que já vem em cada sistema, código comentado com justificativas. |
| A1.1–A1.5.3 | Ok: relógio em ticks; fila global; CPU desligada no Gantt, no resumo e nas estatísticas; TCB único; um snapshot por tick; modos passo a passo e completo. |
| A2.1–A2.5 | Ok: cor por tarefa, pronta sem cor, suspensa preta pontilhada, CPU indicada, marcadores no **instante exato** (agora também no terminal), SVG sem limite de tempo, menor ID embaixo. |
| A3.1–A3.4 | Ok: parser tolerante; erros com linha, campo e motivo; valores padrão com aviso; caminho livre; edição validada. |
| A4–A4.4 | Ok: RM e EDF; plugável (tabela de registro + `.so`); desempate na ordem 1→5; aperiódicas ignoradas com aviso; 10 ativações por tarefa. |
| §6 teoria | Ok: deadline absoluto; perda marcada e a ativação continua; Tabela 2.1 e Figura 2.6 conferidas pelos testes. |
| §7 compilação | Ok: flags do steering e valgrind limpo. |
| §11 preparação para o B | Ok: prioridade ativa × nominal, lista dinâmica de eventos por tarefa, estado Suspensa. |

## O que foi alterado

### 1. Quantum × desempate (A4.3, ambiguidade 1)

**Antes:** quem esgotava o quantum ia para trás das tarefas com a mesma prioridade, por cima dos
critérios 2 a 4. Com RM, 1 CPU e quantum 2:

```
1;E74C3C;0;4;20;5     <- prazo 5
2;3498DB;0;4;20;20    <- prazo 20
```

saía `[0,2)T1 [2,4)T2 …`, e a T1 perdia o prazo em todas as ativações.

**Agora:** ao fim do quantum o escalonador decide de novo e, pelo critério 1, a mesma tarefa
continua se ainda for a mais prioritária. O mesmo exemplo dá `[0,4)T1 [4,8)T2`, sem perdas. O
quantum continua observável pelo evento **"fim de quantum"**, que aparece na lista de eventos do
passo a passo.

### 2. Prioridade nominal × ativa (§11)

- O contrato do escalonador mudou (`include/sched.h`). Antes ele era uma função de comparação
  `(a, b)`; agora é `int prioridade(const TCB *k, int agora)`, que devolve a **prioridade
  nominal** (menor = mais prioritária). O RM devolve o período, o EDF devolve o deadline absoluto
  e o plugin FIFO devolve a chegada da ativação.
- O TCB ganhou `prio_nominal` e `prio_ativa`. `sched_atualizar` (em `sched_tie.c`) calcula a
  nominal e copia para a ativa. É nesse ponto que a herança de prioridade do Projeto B
  (PIP/PCP) vai entrar.
- O escalonador compara sempre a **ativa** e depois aplica o desempate comum.
- O comando `t id` do passo a passo mostra as duas prioridades.

### 3. Lista dinâmica de eventos (§11, A3.3.5)

- `lista_eventos` é separada por `,` ou `;` num vetor dinâmico, sem limite de itens
  (`TarefaCfg.lista_ev/nev`). O TCB aponta para esse vetor (`eventos/neventos`) e continua
  copiável com `memcpy` nos snapshots.
- Os itens não são interpretados nem rejeitados, porque o formato só sai no Projeto B. O texto
  cru também é mantido.
- Novas funções `config_definir_eventos` e `config_liberar_tarefa`. As tarefas criadas durante a
  simulação ficam guardadas numa `Config` auxiliar que dura até o fim.
- O comando `t id` mostra os eventos, ex.: `eventos (Projeto B): 2: ML01:2 | MU01:3`.

### 4. Marcadores no instante exato no terminal (A2.2)

Os marcadores agora ficam na coluna do instante do evento (a coluna t começa no instante t),
como já era no SVG. Antes, o fim de ativação aparecia uma coluna antes. Quando a janela chega ao
fim da simulação, a linha de marcadores ganha uma coluna extra para os eventos do instante final.

### 5. Makefile (§7)

`CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2 -g -Iinclude`.

### 6. Limite de CPUs visível (ambiguidade 6)

O prompt agora mostra `quantidade de CPUs (1 a 64)`, e a tela de configuração mostra
`N CPU(s) (máx. 64)`.

### 7. Critério 3 do desempate

Continua usando o campo **`ingresso`** da tarefa. Cheguei a trocar para a chegada da ativação
atual, mas o enunciado fala em "instante de ingresso **da tarefa**" e define `ingresso` como "o
instante de tempo que a tarefa foi criada". Por isso a mudança foi desfeita.

### 8. Exemplos

- `exemplos/quantum_rodizio.txt` foi renomeado para `exemplos/quantum_empate_total.txt`. São
  duas tarefas iguais em tudo: o sorteio decide quem começa e o quantum não faz rodízio.
- Novo exemplo `exemplos/quantum_respeita_prazo.txt`: o fim do quantum não passa por cima do
  prazo. Também existe como `tests/validos/19_quantum_respeita_prazo.txt`.

### 9. Plugins fechados na saída

Os handles do `dlopen` são fechados com `atexit`, para o valgrind não acusar memória pendurada.

## Arquivos alterados

`Makefile`, `README.md`, `docs/PLANO.md`, `include/{config,gantt,sched,sim,task}.h`,
`src/core/{config,sim}.c`, `src/gantt/gantt_tui.c`,
`src/sched/{sched_edf,sched_rm,sched_tie,sched_registro}.c`, `src/ui/ui.c`,
`plugins/exemplo_fifo.c`, `tests/run_tests.sh`. Novos: `exemplos/quantum_respeita_prazo.txt`,
`tests/validos/19_quantum_respeita_prazo.txt` e este arquivo.

## Comparação com o enunciado oficial (PDF)

O arquivo `docs/simulador-escalonamento-v20260830-enunciado.pdf` é a **versão 0.6**, a mesma em
que o steering se baseia. Os requisitos gerais 1–6 e os requisitos 1 a 5 do Projeto A, com todos
os subitens, batem com o steering, e o código atende cada um. Na leitura do texto original
apareceram três pontos que o steering resumiu ou interpretou:

- **Quantum:** o PDF define `quantum` como "o período máximo de tempo que uma tarefa pode
  executar". O padrão do steering (a mesma tarefa continua se ainda for a mais prioritária) é uma
  interpretação disso e precisa do aval do professor.
- **3.1, editar eventos durante a simulação:** o PDF lista "eventos como mutex, E/S…" entre o que
  é configurável **antes e durante** a simulação. Antes da simulação dá para editar a
  `lista_eventos` (menu de configuração). Durante a simulação, o comando `e id` não tem essa
  opção. Como o 3.3.5 joga os eventos para o Projeto B, isso fica a confirmar.
- **4.2:** o PDF diz que "idealmente o escalonador é apenas uma função que retorna qual é a
  próxima tarefa". Aqui ele é uma função que devolve a prioridade da tarefa, porque com várias
  CPUs são escolhidas as M melhores. Atende o "sem modificar o código da simulação" e o carregamento
  por biblioteca dinâmica, mas não é literalmente "retorna a próxima tarefa".

## Testes antes do commit (2026-10-09)

Feitos em Ubuntu 24.04 nativo. Esta máquina **não é WSL**, então o teste no WSL fica com você.

| Teste | Resultado |
|---|---|
| `make clean && make && make plugins` | 0 warnings com `-pedantic -O2` |
| `make test` | 55/55 |
| valgrind, modo completo, 9 exemplos + 16 arquivos válidos | 25/25 sem erros ou vazamentos |
| valgrind, 16 arquivos inválidos (`--validar`) | 16/16 recusados (saída 1), sem vazamentos |
| valgrind, passo a passo (avançar, voltar, ir para, suspender/retomar, adicionar tarefa, trocar algoritmo, quantum e CPUs, exportar, rodar até o fim) | ok |
| valgrind, menu de configuração (editar e remover tarefa, nova tarefa, entradas inválidas) | ok |
| valgrind, pedir arquivo (inválido → inexistente → válido) | ok |
| valgrind com plugin `.so` | ok |
| Arquivo com espaço no caminho, fora do projeto, com extensão `.CFG` | ok |
| SVGs gerados são XML válidos | ok |
| AddressSanitizer + UBSan em todos os exemplos e válidos | ok |

## O que ainda depende de você

1. **Commitar** as mudanças e o `docs/steering.md`, que ainda não estão no git. O
   `exemplos/quantum_rodizio.txt` foi renomeado com `git mv`.
2. **Confirmar com o professor:** a lista está na seção abaixo.
3. **Testar no WSL** (`make`, `make test`) e rodar o valgrind antes da defesa: `sudo apt install valgrind`, depois
   `valgrind --leak-check=full ./projetoSO exemplos/fig2_5_rm.txt --modo completo`.
4. **Projeto B** (mutex, E/S, aperiódicas, PIP/PCP): depende do enunciado. Os ganchos já estão
   prontos (prioridade ativa, lista de eventos, estado Suspensa).

Observação: `LIMITE_TICKS` (2.000.000) continua existindo, mas é só uma trava contra laço
infinito no "rodar até o fim". Não limita o Gantt nem a imagem (A2.4), e uma simulação normal
nunca chega perto dele.

## Perguntas para o professor

Cada ponto foi conferido contra o capítulo "Escalonamento de Tempo Real"
(`docs/cap2-escalonamento-tempo-real.md`) e o enunciado.

### Perguntar

**Ideia geral:** o enunciado define critérios de desempate "para todos os algoritmos", mas no RM
a prioridade vem do período e no EDF vem do deadline absoluto. Algumas regras genéricas ficam
ambíguas, ou até se contradizem, quando aplicadas a esses dois algoritmos específicos.

**Impacto:** o 1 e o 4 mudam o que aparece na tela. O 2 e o 3 mudam só desempates raros. O 5, o
6 e o 7 são formalidade ou mudanças fáceis.

#### 1. Quantum: o que ele deve fazer no RM/EDF?

- **Enunciado:** `quantum` é "o período máximo de tempo que uma tarefa pode executar".
- **Por que é dúvida:** quantum é um conceito de escalonadores por fatia de tempo (Round-Robin).
  No RM e no EDF uma tarefa só perde a CPU para outra **mais prioritária**, e o capítulo nem
  menciona quantum. Se o quantum tirasse a tarefa da CPU, sobrariam duas leituras, e as duas dão
  problema:
  - **ceder para qualquer tarefa:** uma tarefa de prioridade menor tomaria a CPU de uma de
    prioridade maior, o que deixa de ser RM/EDF;
  - **ceder só para as empatadas (rodízio):** o rodízio só existe quando há empate, e o empate é
    justamente onde o critério 1 manda **manter quem já estava executando**. As duas regras
    dizem coisas opostas na mesma situação.
- **Exemplo:** duas tarefas RM com período 20 e duração 6, quantum 2.
  - Hoje: `[0,6) T1  [6,12) T2`.
  - Com rodízio: `[0,2) T1  [2,4) T2  [4,6) T1 …`.

  Na versão antiga do código, que fazia rodízio, uma tarefa com prazo 5 perdia o deadline em
  toda ativação (ver o exemplo do item 1 de "O que foi alterado").
- **Como está hoje:** quando o quantum acaba, o escalonador é chamado de novo e a mesma tarefa
  continua se ainda for a melhor. Fica registrado o evento "fim de quantum". Na prática o quantum
  não muda o resultado; vira só um ponto extra de reescalonamento.
- **Pergunta:** o que ele quer que o quantum faça no RM/EDF? Provavelmente o campo existe porque o
  formato do arquivo vai servir para outros algoritmos no Projeto B, como o PRIOp citado no
  enunciado.

#### 2. Critério 2: "prazo" é absoluto ou relativo?

- **Os dois significados:**
  - prazo **relativo** (D): o valor do arquivo, por exemplo "10 ticks a partir da chegada";
  - deadline **absoluto**: o instante no relógio, chegada + D. Uma ativação que chegou em t=5 com
    D=10 tem deadline absoluto 15.
- **Hoje:** usa o absoluto, que é o que o capítulo usa para ordenar: d_ik = (k−1)P_i + D_i; no EDF,
  "o deadline mais próximo do tempo atual".
- **Por que é dúvida:** com o absoluto, o critério 2 **nunca faz nada no EDF**. O EDF já escolhe
  pelo deadline absoluto, então duas tarefas só empatam se tiverem o mesmo deadline absoluto, e aí
  o critério 2 empata de novo. Ele só tem efeito no RM. Como o enunciado diz que os critérios valem
  "para todos os algoritmos", talvez o professor estivesse pensando no relativo, que funciona nos
  dois.
- **Exemplo no EDF:**
  - A chegou em t=0 com D=10, deadline absoluto 10;
  - B chegou em t=5 com D=5, deadline absoluto 10.

  O EDF empata. Pelo critério 2 absoluto, empata de novo e a decisão passa para o critério 3.
  Pelo critério 2 relativo, B vence, porque D=5 < D=10.

#### 3. Critério 3: "ingresso" é a criação da tarefa ou a chegada da ativação?

- **Enunciado:** "o instante de ingresso da tarefa: quem chegou antes é escolhida". O campo
  `ingresso` é definido como "o instante em que a tarefa foi criada".
- **Por que é dúvida:** a frase mistura dois conceitos. "Ingresso da tarefa" aponta para o campo do
  arquivo, um valor fixo. "Quem chegou antes" sugere a ordem de chegada na fila de prontos, ou
  seja, a chegada da **ativação atual**, que muda a cada período. O capítulo distingue os dois.
  Hoje seguimos a leitura literal, o campo `ingresso`.
- **Exemplo em que dão resultados diferentes (EDF):**

  | Tarefa | ingresso | período | prazo | ativação atual | deadline absoluto |
  |---|---|---|---|---|---|
  | A | 5 | 15 | 20 | chegou em 20 | 40 |
  | B | 0 | 10 | 10 | chegou em 30 | 40 |

  As duas empatam em 40. Pelo ingresso, B vence, porque foi criada antes (0 < 5). Pela chegada da
  ativação, A vence, porque está esperando desde 20.
- No RM isso quase nunca aparece: para empatar, os períodos precisam ser iguais, e aí as duas
  ordens coincidem.

#### 4. Tarefa entre ativações: vazio ou "suspensa"?

- **A situação:** uma tarefa periódica terminou a ativação atual e está esperando o próximo
  período. Hoje esse trecho fica **vazio** no Gantt.
- **Por que é dúvida:** as duas leituras têm base.
  - **Vazio:** é assim que as figuras 2.5 e 2.6 do capítulo desenham.
  - **Suspensa (preto pontilhado):** o enunciado manda mostrar como suspensa a tarefa parada "por
    qualquer motivo" e pede o estilo do Maziero. No modelo de estados do Maziero, uma tarefa
    esperando um temporizador está suspensa.
- **Impacto:** é visual, mas grande. Com tarefas de baixa utilização, a maior parte do Gantt
  passaria a ser preto pontilhado. Antes do ingresso e depois da 10ª ativação continuaria vazio de
  qualquer jeito.

#### 5. N = 64 CPUs

- **Por que é dúvida:** o enunciado diz "no mínimo 1 e no máximo N", mas não diz quanto é N.
  Escolhemos 64 (`MAX_CPUS` em `include/config.h`), que aparece na tela.
- O professor pode querer outro valor, ou que o N também seja configurável. O risco é baixo, e
  trocar é mudar uma constante.

#### 6. Editar eventos durante a simulação

- **Por que é dúvida:** o enunciado se contradiz um pouco.
  - O **3.1** lista "eventos como mutex, E/S, envio/recebimento" entre o que deve ser
    configurável **antes e durante** a simulação.
  - O **3.3.5** diz que a lista de eventos "vai ser tratada no projeto B".
- **Como está hoje:** dá para editar a `lista_eventos` **antes** de simular, no menu de
  configuração. Durante a simulação, o comando `e id` não tem essa opção, até porque no Projeto A
  os eventos não fazem nada.
- **Risco:** ele pedir isso na demonstração. Se precisar, é rápido de acrescentar.

#### 7. Escalonador plugável

- **Enunciado:** "idealmente, o escalonador é apenas uma função que retorna qual é a próxima
  tarefa a ser executada".
- **Como está hoje:** o escalonador é uma função que devolve a **prioridade** de uma tarefa. O RM
  devolve o período e o EDF devolve o deadline. O simulador ordena e escolhe.
- **Por que fizemos diferente:**
  - com várias CPUs não basta "a próxima": é preciso escolher as **M melhores**, uma para cada
    CPU;
  - o desempate (critérios 1 a 5) é o mesmo para todos os algoritmos e fica num lugar só, então um
    algoritmo novo não tem como errar o desempate;
  - o capítulo apresenta RM, EDF e DM justamente como formas diferentes de **atribuir
    prioridade** sobre o mesmo mecanismo.
- **Por que é dúvida:** o texto diz "idealmente", então não é obrigatório, mas é diferente do
  sugerido. O requisito em si é cumprido: dá para criar um algoritmo novo sem mexer na simulação,
  inclusive como biblioteca `.so`. Ainda assim, vale ouvir dele se aceita esse desenho.

### Já confirmado pelo capítulo (basta mencionar, se surgir)

- **Ativação nova antes de a anterior terminar:** fica acumulada e conta para as 10 execuções. A
  §2.5.3 (deadline arbitrário) assume que "uma liberação só executa após as anteriores da mesma
  tarefa terminarem".
- **RM com prazo diferente do período:** a prioridade é pelo período ("prioridade decresce com o
  aumento do período"). Prioridade pelo prazo é o Deadline Monotonic, outro algoritmo. Com
  D_i ≠ P_i o RM continua ordenando por período, só deixa de ser ótimo.
- **Convenção do tick ([t, t+1), terminar até o fim do tick d−1 cumpre o prazo):** na Figura 2.7
  a tarefa C termina exatamente no deadline 16 e é considerada escalonável (R_C ≤ D_C). Na
  Figura 2.6(b), B perde o prazo porque ainda não terminou em t = 50, o que bate com o teste
  automático.

## Atualização de 2026-10-13 (respostas do professor no Discord)

### Dúvida 1, quantum: resolvida

O professor respondeu que o campo `quantum` tem de ser lido do arquivo e que usá-lo ou não é
"específico de cada escalonador". No RM e no EDF o quantum é lido e guardado, e a escolha da
tarefa continua pela prioridade e pelo desempate. O evento "fim de quantum" fica como decisão da
equipe (ele não muda o resultado). Não precisa perguntar de novo.

### Dúvida 4, tarefa entre ativações: decidida pela equipe

O professor disse que a equipe decide e justifica na defesa ("toda fila tem um motivo, uma
semântica"). Decisão: o estado `T_ESPERANDO` foi removido. A tarefa que acabou a ativação fica em
`T_SUSPENSA` com `motivo = SUSP_PERIODO`.

- **Justificativa (defesa):** o Maziero (cap. 4) inclui em "suspensa" tanto a espera por recurso
  quanto a espera pelo tempo (sleep). O motivo dá a semântica e decide a saída:
  - `SUSP_PERIODO` volta sozinho para PRONTA quando a próxima ativação chega;
  - `SUSP_USUARIO` só volta quando o usuário retoma a tarefa.
- **Gantt:** `SUSP_PERIODO` fica vazio, como nas figuras 2.5 e 2.6. O preto pontilhado é só para
  `SUSP_USUARIO`, e `ticks_suspensa` conta apenas essa.
- **Projeto B:** mutex e E/S entram como novos motivos.
- **Testes:** 5 testes novos em `tests/run_tests.sh` (60 no total).

### Ainda abertas

| # | Ponto | Encaminhamento |
|---|---|---|
| 2 | Critério 2: prazo absoluto ou relativo | Decidido: **deadline absoluto** (urgência real da ativação atual); confirmar com o professor só se sobrar tempo |
| 3 | Critério 3: `ingresso` da tarefa ou chegada da ativação | Decidido: campo `ingresso` (como já estava) |
| 5 | Valor de N de CPUs (hoje 64) | Equipe decide e justifica |
| 6 | Editar `lista_eventos` durante a simulação | Acrescentar se ele pedir |
| 7 | Escalonador devolve a prioridade, não "a próxima tarefa" | Equipe decide e justifica |

### Critério 2: fica o deadline absoluto

Uma análise de IA sugeriu o prazo relativo (os critérios 2, 3 e 4 usam nomes de atributos da
tarefa; no EDF o absoluto nunca atuaria). Foi implementado e depois revertido por decisão da
equipe: o deadline absoluto desempata pela **urgência real** da ativação atual, e a redundância no
EDF se explica porque a lista de critérios é genérica (vale para qualquer algoritmo; no RM o
critério atua). As duas leituras se defendem; se o professor responder, ajustar
`sched_desempate` (`src/sched/sched_tie.c`). O critério 3 continua usando o campo `ingresso`.
Teste: `tests/validos/20_desempate_prazo_absoluto.txt`.

## Decisões da equipe para a defesa (2026-10-13)

O professor deixou estes pontos em aberto de propósito: cada escolha vale se for argumentada. Base: capítulo "Escalonamento de Tempo Real" (`docs/cap2-escalonamento-tempo-real.md`, versão do commit `821954b`) e enunciado v0.6.

| # | Ponto | Decisão | Argumento |
|---|---|---|---|
| 2 | Critério 2 do desempate | **Deadline absoluto** da ativação atual | O capítulo ordena por deadline absoluto, d_ik = (k−1)P_i + D_i, e o EDF escolhe "o deadline mais próximo do tempo atual". Desempatar pela urgência real da ativação é coerente com isso. No EDF o critério nunca atua porque a lista de critérios é genérica (vale para qualquer algoritmo); no RM atua. |
| 3 | Critério 3 do desempate | Campo **`ingresso`** da tarefa | O enunciado diz "instante de ingresso da tarefa" e define `ingresso` como o instante em que a tarefa foi criada. É a leitura literal. |
| 5 | N de CPUs | **64** (`MAX_CPUS` em `include/config.h`) | O enunciado não fixa N. 64 cobre qualquer máquina real de aula e o limite aparece na tela; trocar é mudar uma constante. Sem tabelas de tamanho fixo: o resto é alocado dinamicamente. |
| 6 | Editar `lista_eventos` durante a simulação | Só **antes** de simular | O 3.3.5 diz que os eventos são tratados no Projeto B; no A eles não alteram a execução, então editá-los durante a simulação não teria efeito observável. A estrutura (vetor dinâmico por tarefa) já permite acrescentar o comando quando o B definir o formato. |
| 7 | Escalonador devolve prioridade | **Função `prioridade(k, agora)`**, não "a próxima tarefa" | O enunciado diz "idealmente". Com M CPUs e fila global é preciso escolher as M melhores, e o desempate (critérios 1 a 5) fica num lugar só, igual para todo algoritmo. O capítulo trata RM, EDF e DM como formas de atribuir prioridade sobre o mesmo mecanismo. Novos algoritmos entram por tabela de registro ou por `.so` sem tocar na simulação. |
| 1 | Quantum | Lido e guardado; RM/EDF decidem só por prioridade | Resposta do professor: usar o quantum é "específico de cada escalonador". |
| 4 | Entre ativações | `T_SUSPENSA` com `motivo = SUSP_PERIODO`, desenhada vazia | Maziero inclui espera por tempo em "suspensa"; o Gantt segue as Figuras 2.5 e 2.6 do capítulo. |
| 8 | Interface | **Janela gráfica** feita em C, sem biblioteca de interface; o terminal continua com `--terminal` | O enunciado pede "visualização gráfica" (req. 3 e 2.x) e interface intuitiva (req. 5), mas proíbe instalar biblioteca extra (req. 5 dos gerais). GTK, Qt ou SDL exigiriam instalação. Por isso o programa desenha tudo sozinho e usa só o que já vem em cada sistema para abrir a janela. Detalhes na seção abaixo. |

Também fica registrado: o repositório agora tem `.gitattributes` com `eol=lf`, porque no Windows o `core.autocrlf=true` convertia `tests/run_tests.sh` e os exemplos para CRLF e o `make test` quebrava no WSL. O arquivo `tests/validos/05_finais_de_linha_windows.txt` é exceção (`-text`), pois testa justamente CRLF.

## Atualização de 2026-10-14: interface gráfica

### Decisão

Por padrão, o programa abre uma **janela gráfica** com as mesmas funções da interface de
terminal: escolher o arquivo, revisar e editar a configuração, rodar passo a passo ou a execução
completa, editar tarefas durante a simulação e exportar o SVG. A interface de terminal continua
igual e é usada com `--terminal`, com `--modo` (scripts e `make test`) ou quando não há servidor
gráfico (o programa avisa o motivo e segue no terminal).

### Por que não precisa instalar nada

- **Desenho e fonte**: o próprio programa pinta a imagem em memória (`src/gfx/tela.c`) com uma
  fonte 8x16 embutida no código (`src/gfx/fonte.c`, fonte "Fixed" do X11, domínio público).
- **Janela**, um arquivo por sistema, cada um compilado só no sistema dele (`#ifdef`):
  - Linux/WSL: `src/gfx/gfx_x11.c` fala o protocolo X11 direto no socket do servidor gráfico,
    com `socket`/`write`/`read` da libc, sem libX11;
  - Windows: `src/gfx/gfx_win32.c` usa a API Win32 (`user32`/`gdi32`), parte do Windows;
  - macOS: `src/gfx/gfx_cocoa.c` usa o Cocoa pelo runtime do Objective-C, chamado de C.
- **Executável Linux**: depende só da `libc.so.6` (glibc 2.34 ou mais nova). O `-ldl` do
  Makefile não é um pacote à parte: faz parte da glibc e só existe por causa dos plugins.
- **Executável Windows** (compilado com `-static`): depende só de `KERNEL32`, `USER32`, `GDI32` e
  `msvcrt`, todas do próprio Windows.

### Requisito "executar sem instalar" (req. 5 dos gerais)

O enunciado fala em **executar** sem instalar biblioteca, software ou runtime, e não fala em
`make` nem em compilar. Compilar exige o `gcc`, que é instalado (`build-essential`, MSYS2). Para
não depender disso, a equipe pode **entregar os executáveis já compilados** (Linux e Windows), com
o código-fonte e o Makefile junto. A confirmar com o professor: se ele vai rodar no PC dele ou se
a equipe apresenta no próprio notebook.

### Outras mudanças

- As regras de edição de tarefa (validações, período, prazo, ingresso, suspender/retomar e
  encerrar) foram para `src/core/edicao.c`. O terminal (`ui.c`) e a janela (`ui_gui.c`) usam o
  mesmo código.
- O Gantt da janela (`src/gantt/gantt_gfx.c`) usa os mesmos símbolos do SVG e mostra também a
  linha do instante atual.
- O Makefile detecta o sistema e escolhe as bibliotecas do sistema (`-lgdi32` no Windows,
  frameworks no macOS). Para gerar o `.exe` a partir do Linux:
  `make CC=x86_64-w64-mingw32-gcc TARGET=projetoSO.exe`.

### Arquivo no pendrive (req. 3.3.4)

O professor pode trazer o arquivo de configuração num pendrive. O caminho sempre pôde ser
qualquer um (digitado na janela, passado na linha de comando ou, no Windows, algo como
`E:\teste.txt`), mas chegar até o pendrive pela lista de arquivos exigia muitos cliques em
"Pasta acima" e, no Windows, não dava para trocar de unidade. Por isso a tela de abrir arquivo
ganhou atalhos:

| Botão | Linux | Windows | macOS |
|---|---|---|---|
| Pendrives e discos | `/media/<usuário>` (ou `/run/media/<usuário>`; sem eles, `/media` ou `/mnt`) | lista de unidades com o tipo, ex.: `E:  (pendrive / removível)` | `/Volumes` |
| Pasta pessoal | `$HOME` | `%USERPROFILE%` | `$HOME` |
| Pasta do programa | pasta onde o programa foi aberto | idem | idem |
| Pasta acima | sobe até `/` | sobe até `E:/` e depois volta à lista de unidades | sobe até `/` |

- A lista agora usa caminhos absolutos (antes subir gerava `../../..`), e o arquivo carregado
  aparece com o caminho completo na configuração.
- A lista de unidades do Windows vem de `GetLogicalDrives`/`GetDriveTypeA` (`gfx_unidades` em
  `src/gfx/gfx_win32.c`), da própria API do Windows. No Linux e no macOS a função devolve 0,
  porque lá os pendrives aparecem como pastas.
- Sem pendrive conectado, a lista avisa: "nenhum pendrive encontrado: conecte-o e clique em
  'Pendrives e discos' de novo".
- Ainda não suportado na caixa de texto: colar (Ctrl+V) e, no Linux, letras acentuadas feitas
  com tecla morta (ã, é, ô). Nesses casos, basta clicar no arquivo na lista.

### Como foi verificado

| Sistema | Situação |
|---|---|
| Linux (X11) | Testado na janela real, com capturas de todas as telas, inclusive no tamanho mínimo. Sessão completa no valgrind: 0 erros e 0 vazamentos, e o X da janela encerra com código 0. `make test`: 62 de 62 testes passam, compilando sem warnings. |
| WSL no Windows 11 | Usa o mesmo código do Linux, pelo WSLg. Não testado à parte. |
| Navegação até o pendrive | Linux: os quatro atalhos, a subida até `/` e o carregamento pela lista foram testados na janela, mas sem pendrive conectado (só o aviso "nenhum pendrive encontrado"). Windows e macOS: só compilados. |
| Windows nativo | Compila sem warnings com MinGW. **Falta testar num PC com Windows**, inclusive a lista de unidades. O Wine foi usado só como ferramenta de teste nesta máquina e não é dependência do programa. |
| macOS | Só a verificação de sintaxe passou (compilando no Linux com `-D__APPLE__`). **Falta testar num Mac.** |

O bloqueio do Smart App Control a `.exe` sem assinatura (já descrito no README) continua valendo
e não tem relação com bibliotecas.
