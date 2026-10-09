# Steering — Simulador de Escalonamento (SO · UTFPR)

> Fonte da verdade para qualquer agente/IA que trabalhe neste repositório (`projetoSO`).
> Baseado em: enunciado **"Simulação de um Sistema Operacional Multitarefa de Tempo Compartilhado" v0.6** (Prof. Dr. Marco Aurélio Wehrmeister), **Cap. 2 — O Escalonamento de Tempo Real** (material indicado no Moodle) e **Maziero, *Sistemas Operacionais: Conceitos e Mecanismos*** (cap. 4–6).
>
> Regra de ouro: **nenhum requisito marcado como OBRIGATÓRIO pode ser quebrado**. Na dúvida, consulte a seção [Ambiguidades](#9-ambiguidades-e-decisões-a-confirmar) antes de inventar comportamento.

---

## 1. Contexto do projeto

- Disciplina: Sistemas Operacionais — UTFPR. Entrega em duas etapas: **Projeto A** (atual) e **Projeto B** (enunciado ainda não divulgado; sai até a Defesa A).
- Linguagem escolhida: **C**. Compilação e execução no **WSL (Ubuntu)** com `gcc` + `make`.
- Repositório: `github.com/GuilhermeOliverr/projetoSO` (público).
- O que se constrói: um **software que simula** um SO multitarefa, preemptivo, de tempo compartilhado, com **múltiplas CPUs**, escalonando tarefas periódicas com **RM** e **EDF**, com visualização em **gráfico de Gantt**, modo passo a passo com avançar/retroceder e edição de estado em tempo de simulação.

---

## 2. Objetivos pedagógicos (do enunciado)

1. Aprofundar o estudo de gerência de tarefas implementando escalonadores, a execução de múltiplas tarefas e do escalonador, e a apresentação gráfica dessa execução ao longo do tempo.
2. Aprimorar o conhecimento sobre interação entre tarefas usando serviços de exclusão mútua (foco do Projeto B).
3. Compreender e resolver problemas de escalonamento: inanição, inversão de prioridades, impasse etc. (foco do Projeto B).

O código deve deixar evidente que esses conceitos foram entendidos — não basta funcionar.

---

## 3. Requisitos gerais (OBRIGATÓRIOS em A **e** B)

| ID | Requisito |
|---|---|
| **G1** | Simular a execução de tarefas em um SO **multitarefa preemptivo de tempo compartilhado**. |
| **G2** | O computador tem **mais de um processador**. Quantidade configurada pelo usuário: **mínimo 1, máximo N**. |
| **G3** | **Visualização gráfica** da execução **ao longo do tempo, por processador**. Todo evento relevante aparece com um **elemento gráfico distinto por tipo de evento**. Exibir **legenda**. Indicar **claramente em qual CPU** cada tarefa executa. |
| **G4** | Software **configurável**; usuário controla a simulação e as características do sistema. Deve ser possível **avançar e retroceder**; o **estado de cada tarefa pode ser modificado manualmente em qualquer passo**. |
| **G5** | Linguagem livre, mas o executável **roda sem instalar nenhuma biblioteca extra, runtime ou software**. → Em C: usar **apenas a biblioteca padrão (libc) e APIs do sistema**. Nada de SDL, GTK, ncurses, libpng, cairo etc. |
| **G6** | **Todo o código comentado**: o que cada parte/função/componente faz **e por quê** (justificativa de cada decisão de implementação). |

---

## 4. Requisitos do Projeto A

### 4.1 Núcleo da simulação (req. 1)

| ID | Requisito |
|---|---|
| **A1** | Simula SO multitarefa preemptivo de tempo compartilhado. |
| **A1.1** | **Relógio global** em **ticks**. A cada tick, verificar se há ação/evento a tratar. O tempo simulado **não** está atrelado ao tempo real (pode avançar/retroceder instantaneamente). |
| **A1.2** | Múltiplas CPUs. O escalonador **minimiza ociosidade**: **nunca** pode haver tarefa na fila de prontos com CPU livre. **Fila de prontos global** (não por CPU). CPU sem tarefa atribuível é **desligada**; o **período em que cada CPU fica desligada** deve ser mostrado ao usuário **e** no Gantt. |
| **A1.3** | Todas as informações de cada tarefa (antes, durante e depois da simulação) ficam em **uma única estrutura de dados** por tarefa — o **TCB** (*Task Control Block*). |
| **A1.4** | A simulação respeita as características temporais de cada tarefa: ingresso, duração, período, prazo, instantes de eventos. |
| **A1.5** | Dois modos: **(a) passo a passo** e **(b) execução completa sem intervenção humana**. |
| **A1.5.1** | No passo a passo, permitir **examinar o estado atual do sistema e de cada tarefa individualmente** — funciona como um **debugger** do sistema. |
| **A1.5.2** | No passo a passo, **avançar e retroceder** à vontade. O **histórico do estado completo do sistema a cada passo** fica **em memória** e é usado para avançar/retroceder. Avançar/retroceder **altera o relógio global**. O usuário pode **modificar o estado atual ou as características de qualquer tarefa em qualquer passo**. |
| **A1.5.3** | Na execução completa, mostrar **apenas o resultado final** (sem passos intermediários). |

### 4.2 Visualização — Gráfico de Gantt (req. 2)

| ID | Requisito |
|---|---|
| **A2.1** | Gantt mostrando as tarefas ao longo do tempo **e a CPU usada em cada trecho de execução**. **Executando** → cor da tarefa (vinda da config), **cada tarefa com cor diferente**. **Pronta (na fila, sem executar)** → **ausência de cor**. **Suspensa (qualquer motivo)** → **cor preta com preenchimento diferente** do de execução (ex.: pontilhado em vez de sólido). |
| **A2.2** | **Chegada** de tarefa e **término** de tarefa claramente marcados (ícone, seta, figura…), **no instante exato** em que ocorrem. |
| **A2.3** | Atualização do Gantt compatível com o modo: no passo a passo, **Gantt atualizado na tela a cada passo**. *(O enunciado cita "requisito 1.4", mas o correto é 1.5.)* |
| **A2.4** | Ao final, **gerar arquivo de imagem** (JPG/PNG/SVG/…) com o Gantt **de toda a simulação**. **Não pode ser print da janela**, e **não pode ter limite de tempo máximo** de simulação (a imagem cresce conforme necessário). |
| **A2.5** | Visual **semelhante às figuras da seção 6.4 do livro do Maziero** (ver §6.3). **Eixo Y em ordem decrescente de ID**: o **menor ID fica mais próximo do eixo X** (embaixo); o segundo menor logo acima; e assim por diante. |

### 4.3 Configuração e controle (req. 3)

| ID | Requisito |
|---|---|
| **A3.1** | Configurar **antes e durante** a simulação: conjunto de tarefas, características de cada tarefa, algoritmo, eventos (mutex, E/S, envio/recebimento de dados, etc.). Se uma modificação **não fizer sentido ou não puder ser aplicada** durante a execução, **informar claramente o motivo**. |
| **A3.2** | **Valores padrão sugeridos para todos os parâmetros** (da simulação e do próprio software). Valores do usuário sobrescrevem os padrões — tanto ao carregar o arquivo quanto nas telas de edição. |
| **A3.3** | Configuração carregada de **arquivo texto simples** no formato da §5. |
| **A3.3.1** | 1ª linha = parâmetros do sistema. Da 2ª em diante = uma tarefa por linha. **Mínimo 2 linhas**; **sem limite** de tarefas. |
| **A3.3.2** | Strings **case-insensitive**: `"PRIOP"`, `"priop"`, `"PrioP"` são iguais. |
| **A3.3.3** | Qualquer linha **pode ou não** terminar com `;`. Tratar os dois casos. |
| **A3.3.4** | Carregar arquivo **com qualquer nome, de qualquer lugar** (qualquer diretório, partição, disco externo/pendrive). **Proibido arquivo fixo.** |
| **A3.3.5** | `lista_eventos` é tratada no Projeto B, mas o parser/estruturas devem prever **inúmeros eventos por tarefa**. |
| **A3.3.6** | Erros de carregamento ou no conteúdo do arquivo: **mostrar claramente o motivo** (idealmente com número da linha e campo). **Espaços e linhas em branco não são erro** — ignorar. |
| **A3.3.7** | ⚠️ **CRÍTICO**: qualquer falha ao carregar um arquivo **válido** é **falha grave e impede a demonstração**. Testar exaustivamente o parser. |
| **A3.4** | Usuário pode **modificar o estado atual de qualquer tarefa em qualquer passo**. Modificação semanticamente inválida ou impossível → **avisar claramente**. |

### 4.4 Algoritmos de escalonamento (req. 4)

| ID | Requisito |
|---|---|
| **A4** | Implementar **Rate Monotonic preemptivo** (string `RM`) e **Earliest Deadline First preemptivo** (string `EDF`). Ver §6. |
| **A4.1** | Usuário escolhe o algoritmo **antes** da simulação. |
| **A4.2** | Mecanismo **plugável**: novos algoritmos entram **sem modificar o código da simulação**. Idealmente o escalonador é **só uma função que retorna a próxima tarefa**, podendo inclusive estar numa **biblioteca dinâmica** fora do executável. |
| **A4.3** | **Desempate** (vale para todos os algoritmos), **nesta ordem**: **(1)** a tarefa que estava executando imediatamente antes do escalonamento (evita troca de contexto para ela mesma); **(2)** o **prazo** da tarefa; **(3)** **instante de ingresso** — quem chegou antes; **(4)** **duração** — menor primeiro; **(5)** **sorteio** — quando ocorrer, mostrar **elemento gráfico no Gantt** no instante em que a tarefa sorteada **(re)começa** a executar. |
| **A4.4** | No Projeto A, **somente tarefas periódicas**. Tarefas aperiódicas (`periodo = 0`) são **ignoradas** e isso é **reportado claramente ao usuário**. Para encerrar a simulação, **uma tarefa periódica termina após executar 10 vezes** (10 ativações/jobs concluídos). |

### 4.5 Interface (req. 5)

| ID | Requisito |
|---|---|
| **A5** | Interface **intuitiva**, fácil de entender e usar. **Todo erro** durante o uso é mostrado de forma clara, **com o motivo**. |

---

## 5. Formato do arquivo de configuração

```
algoritmo_escalonamento;quantum;qtde_cpus
id;cor;ingresso;duracao;periodo;prazo;lista_eventos
id;cor;ingresso;duracao;periodo;prazo;lista_eventos
...
```

| Campo | Significado | Validação |
|---|---|---|
| `algoritmo_escalonamento` | Algoritmo a usar (`RM`, `EDF`; outros no futuro) | case-insensitive; desconhecido → erro claro |
| `quantum` | Tempo máximo que uma tarefa pode executar continuamente | inteiro > 0 |
| `qtde_cpus` | Nº de processadores | inteiro entre 1 e N (N = constante configurável do simulador) |
| `id` | Identificador **único** da tarefa | duplicado → erro |
| `cor` | Cor RGB em hexadecimal, ex. `F0E0D0` (R=F0, G=E0, B=D0) | 6 dígitos hex, case-insensitive; aceitar com/sem `#` é tolerância opcional |
| `ingresso` | Instante de criação da tarefa (1ª ativação) | inteiro ≥ 0 |
| `duracao` | Tempo de execução de cada ativação (Ci) | inteiro > 0 |
| `periodo` | **> 0**: periódica, ativada a cada *n* ticks. **= 0**: aperiódica (ativada uma vez na criação) → **ignorar no Projeto A com aviso**. **< 0**: **erro no arquivo** | inteiro |
| `prazo` | Deadline relativo de cada ativação; **conta a partir da ativação** | inteiro > 0 |
| `lista_eventos` | Eventos durante a execução (Projeto B) | armazenar sem quebrar; campo pode estar vazio ou ausente |

Regras de parsing (todas obrigatórias):
- Linhas e espaços em branco são ignorados (inclusive espaços em volta dos campos).
- `;` final opcional em **qualquer** linha.
- Comparação de strings case-insensitive.
- Erros apontam **linha, campo e motivo**.
- Caminho do arquivo informado pelo usuário (argumento de linha de comando e/ou prompt na interface). **Nunca** caminho fixo.
- Campos ausentes recebem **valor padrão** (A3.2), e o usuário é informado de que o padrão foi aplicado.

Exemplo válido:
```
RM;2;2
1;FF0000;0;2;5;5
2;00AA00;0;3;10;10;
3;0000FF;1;4;20;15
```

---

## 6. Base teórica obrigatória

### 6.1 Modelo de tarefa periódica (Cap. 2, §2.2)

Tarefa periódica Ti = (Ci, Pi, Di), com a 1ª ativação no instante de `ingresso`.
- Ativação *k* (k = 0, 1, …, 9): `chegada_k = ingresso + k·Pi`.
- Deadline absoluto da ativação *k*: `d_k = chegada_k + Di` (prazo conta da ativação).
- Cada ativação precisa de `Ci` ticks de CPU.
- **Perda de prazo**: se a ativação não terminou até `d_k`, registrar o evento e **mostrar no Gantt**; a ativação **continua executando** até completar `Ci` (enunciado, campo `prazo`).

Utilização: `Ui = Ci / Pi`; `U = Σ Ui`. (Exibir U ao usuário é um extra útil, não obrigatório.)

### 6.2 Algoritmos

**Rate Monotonic (RM)** — prioridade **fixa**, preemptivo, on-line.
- **Menor período → maior prioridade.** A prioridade não muda durante a simulação.
- Teste suficiente (monoprocessador): `U ≤ n(2^(1/n) − 1)` (≈ 0,69 para n grande). Exato para períodos harmônicos: `U ≤ 1`.
- Preempção: a chegada de uma ativação de tarefa com período menor tira da CPU a tarefa de período maior.

**Earliest Deadline First (EDF)** — prioridade **dinâmica**, preemptivo, on-line.
- **Deadline absoluto mais próximo → maior prioridade.** Recalcular a cada chegada de ativação.
- Teste exato (monoprocessador, Di = Pi): `U ≤ 1`.

Ambos com as premissas clássicas: tarefas periódicas, independentes, Ci conhecido e constante, troca de contexto com custo zero.

Os testes de escalonabilidade acima são para **1 CPU**. Com várias CPUs e fila global, eles **não garantem** nada — servem só como informação. O simulador **não** deve recusar conjuntos com base neles; deve simular e mostrar as perdas de prazo.

### 6.3 Estados e TCB (Maziero, cap. 4–5)

Estados do ciclo de vida: **Nova → Pronta → Executando → (Pronta | Suspensa | Terminada)**, **Suspensa → Pronta**.
- *Executando → Pronta*: fim de quantum ou preempção por tarefa mais prioritária.
- *Executando → Suspensa*: espera por recurso, E/S, sincronização ou tempo.
- *Executando → Terminada*: fim da execução (no Projeto A, após o 10º job).

O TCB guarda: id, estado, cor, parâmetros (ingresso, duração, período, prazo), contabilização (tempo executado no job atual, nº de jobs concluídos, próxima ativação, deadline absoluto atual, CPU atual/última), lista de eventos e histórico necessário ao Gantt.

### 6.4 Estilo visual do Gantt (Maziero §6.4)

Eixo X = tempo em ticks, numerado. Eixo Y = uma linha por tarefa. Quadros **sombreados/coloridos** = execução; quadros **brancos/vazios** = tarefa pronta aguardando CPU; nenhum quadro = tarefa ainda não ingressou ou já terminou. Seguir esse padrão, acrescentando: cor por tarefa, identificação da CPU em cada trecho executado, marcadores de eventos e legenda.

---

## 7. Arquitetura recomendada (C)

```
projetoSO/
├── Makefile
├── README.md            # como compilar, rodar, formato do arquivo, exemplos
├── steering.md          # este arquivo
├── exemplos/            # arquivos .txt de configuração de teste (válidos e inválidos)
└── src/
    ├── main.c           # entrada, laço da interface
    ├── config.c/.h      # parser do arquivo + valores padrão + validação
    ├── tcb.c/.h         # struct TCB e operações sobre tarefas
    ├── sim.c/.h         # relógio, laço por tick, CPUs, fila de prontos global, eventos
    ├── history.c/.h     # snapshots por tick para avançar/retroceder
    ├── sched.h          # interface de escalonador (ponteiro de função)
    ├── sched_rm.c       # RM
    ├── sched_edf.c      # EDF
    ├── sched_registry.c # tabela nome → função; desempate comum (A4.3)
    ├── gantt_tty.c/.h   # Gantt na tela (terminal)
    ├── gantt_svg.c/.h   # exportação da imagem final
    └── ui.c/.h          # menus, inspeção de estado, edição de tarefas
```

Diretrizes:
- **Separação simulação × escalonador (A4.2)**: o simulador só chama algo como
  `int (*escolher)(const Sistema *s, const int *candidatos, int n)` — ou uma função que ordena a fila de prontos — e nunca contém `if (alg == RM)`. Registrar algoritmos numa tabela `{ "rm", sched_rm }`. Opcional: carregar `.so` com `dlopen` (faz parte da glibc, não é biblioteca extra).
- **Desempate (A4.3) centralizado**: um comparador comum aplicado depois do critério primário de cada algoritmo, para todos usarem a mesma ordem. Sorteio com `rand()`, semente configurável (padrão fixo, para a demo ser reproduzível) e evento "sorteio" registrado.
- **Multiprocessador (A1.2)**: a cada tick, escolher as **M** tarefas de maior prioridade entre prontas + executando (M = nº de CPUs). Preferir manter cada tarefa na mesma CPU quando continuar executando. CPU sem tarefa → estado **desligada**, contabilizar períodos.
- **Ordem dentro de um tick** (fixar e documentar): 1) liberar ativações que chegam no tick; 2) checar perdas de prazo; 3) chamar o escalonador e atribuir CPUs; 4) executar 1 tick em cada CPU ligada; 5) registrar conclusões de jobs/tarefas; 6) salvar snapshot.
- **Histórico (A1.5.2)**: snapshot completo (`Sistema` + vetor de TCBs) por tick, em memória dinâmica que cresce sem limite fixo. Retroceder = restaurar snapshot. **Editar no passado descarta o futuro** já gravado (a simulação é recalculada a partir dali) — avisar o usuário.
- **Gantt na tela sem bibliotecas (G5)**: terminal com **sequências ANSI** (cor 24-bit `ESC[48;2;R;G;Bm`) e caracteres (`█` execução, `░`/`·` suspensa em preto, espaço/`□` pronta, `▼`/`↓` chegada, `▲`/`✓` término, `!` perda de prazo, `?` sorteio, número da CPU sobre/abaixo do bloco). Rolagem horizontal para tempos longos.
- **Imagem final (A2.4)**: gerar **SVG escrito à mão** com `fprintf` — texto puro, sem biblioteca, escala ilimitada. Largura proporcional ao tempo total. Incluir legenda, eixos, rótulo de CPU, todos os marcadores. Outra opção sem biblioteca: BMP/PPM (mais trabalho, sem ganho).
- **Memória**: tudo alocado dinamicamente (sem limites fixos de tarefas, ticks ou eventos). Liberar tudo ao sair; testar com `valgrind`.
- **Compilação**: `gcc -std=c11 -Wall -Wextra -pedantic -O2`, sem warnings. Só `-lm` se necessário (libm faz parte da libc).

---

## 8. Elementos gráficos obrigatórios no Gantt (checklist + legenda)

| Situação/evento | Representação sugerida | Requisito |
|---|---|---|
| Tarefa executando | Bloco sólido na cor da tarefa + **nº da CPU** | A2.1, G3 |
| Tarefa pronta (na fila) | Bloco vazio / sem cor (contorno) | A2.1 |
| Tarefa suspensa | Bloco **preto pontilhado/hachurado** | A2.1 |
| Chegada de ativação / nova tarefa | Seta para baixo no instante exato | A2.2 |
| Término de job | Marcador pequeno no instante exato | A2.2 |
| Término da tarefa (10º job) | Marcador destacado (ex. seta para cima / ✓) | A2.2, A4.4 |
| Perda de prazo (deadline miss) | Marcador vermelho / `!` no deadline absoluto | campo `prazo` |
| Deadline absoluto de cada job | Traço fino na linha da tarefa (opcional, ajuda a explicar) | — |
| Desempate por sorteio | Ícone `?`/dado quando a tarefa (re)começa | A4.3 (5) |
| CPU desligada | Linha por CPU com faixa "OFF" + tempo total mostrado ao usuário | A1.2 |
| Preempção | Opcional (o corte do bloco já indica) | G3 |
| **Legenda** com todos os elementos acima | Sempre visível na tela e na imagem | G3 |

Eixo Y: **menor ID embaixo** (perto do eixo X), IDs crescendo para cima (A2.5).

---

## 9. Ambiguidades e decisões a confirmar

O enunciado deixa alguns pontos abertos. Usar o **padrão sugerido** abaixo, documentar no código e no README, e **confirmar com o professor** antes da defesa.

1. **Quantum em RM/EDF.** O arquivo tem `quantum`, mas RM/EDF preemptam por prioridade. *Padrão:* ao fim do quantum o escalonador é chamado de novo; pelo desempate (1) a mesma tarefa continua se ainda for a de maior prioridade. Assim o quantum existe e é observável sem distorcer RM/EDF.
2. **Nova ativação chega antes do job anterior terminar** (overrun após perda de prazo). *Padrão:* ativações pendentes ficam enfileiradas no TCB e executam em ordem (o job seguinte só começa quando o anterior termina — mesma hipótese do Cap. 2, §2.5.3). Cada uma conta para as 10 execuções.
3. **Tarefa periódica entre ativações** (job concluído, esperando o próximo período): *padrão* sem bloco (inativa, como "ainda não ingressou"), já que o enunciado reserva preto-pontilhado para "suspensa". Confirmar se o professor prefere tratá-la como suspensa.
4. **Critério (2) do desempate, "prazo"**: *padrão* deadline **absoluto** do job atual (para EDF coincide com o critério primário; para RM desempata períodos iguais).
5. **RM com prazo ≠ período**: o enunciado manda RM (prioridade por **período**). Não trocar para DM sem pedido.
6. **Valor de N** (máx. CPUs): constante configurável com padrão razoável (ex. 8), informada na interface.
7. **Tick 0 e convenção de intervalos**: um tick `t` representa o intervalo `[t, t+1)`. Deadline em `d` significa que o job precisa terminar até o fim do tick `d−1`.

---

## 10. Padrões de código

- **Comentários em português**, explicando **o que** e **por quê** (G6). Cada arquivo começa com um cabeçalho dizendo sua responsabilidade. Cada função tem comentário de propósito, parâmetros, retorno e decisões tomadas.
- Nomes em português, consistentes com o domínio (`tcb`, `fila_prontos`, `relogio`, `cpu`, `ativacao`, `prazo_absoluto`).
- Funções pequenas, sem variáveis globais mutáveis escondidas (o estado da simulação fica numa struct `Sistema`, o que torna os snapshots triviais).
- Toda entrada do usuário é validada; nenhuma entrada pode derrubar o programa (sem `scanf` cru sem checagem; usar `fgets` + parse).
- Mensagens de erro sempre com **o quê**, **onde** e **por quê** (A3.3.6, A3.4, A5).
- Nada de dependências fora da libc (G5). Não adicionar `#include` de biblioteca de terceiros.

---

## 11. Preparação para o Projeto B

O enunciado do B ainda não saiu, mas já se sabe que envolverá: `lista_eventos`, **mutex** (solicitação/liberação), **E/S**, **envio/recebimento de dados**, **tarefas aperiódicas**, e problemas como **inanição, inversão de prioridades e impasse** (provavelmente PHP/PCP do Cap. 2 §2.6 e servidores aperiódicos do §2.8). O enunciado também cita `PRIOp` como exemplo de string. Portanto:
- O TCB e o laço por tick já devem suportar o estado **Suspensa** e uma lista dinâmica de eventos por tarefa.
- O parser deve guardar `lista_eventos` crua por enquanto, sem rejeitar.
- O escalonador deve receber prioridade **ativa** separada da **nominal** (herança de prioridade virá no B).
- Não acoplar nada que impeça tarefas aperiódicas (`periodo = 0`) de serem simplesmente "ligadas" depois.

---

## 12. Checklist de aceitação para a demonstração

- [ ] Compila sem warnings no WSL com `make`; roda sem instalar nada.
- [ ] Carrega arquivo de **qualquer caminho**; aceita maiúsc./minúsc., `;` final opcional, linhas/espaços em branco.
- [ ] Erros de arquivo mostram linha e motivo; aperiódicas ignoradas com aviso; período negativo = erro.
- [ ] Valores padrão sugeridos para tudo e sobrescrevíveis.
- [ ] RM e EDF preemptivos corretos; desempate na ordem 1→5; sorteio marcado no Gantt.
- [ ] Várias CPUs com fila global; nunca CPU ociosa com tarefa pronta; CPU desligada mostrada (tela + imagem + tempo total).
- [ ] Cada tarefa periódica termina após 10 execuções.
- [ ] Perda de prazo marcada; job continua até completar.
- [ ] Modo passo a passo: Gantt atualizado a cada passo, inspeção do sistema e de cada TCB, avançar e retroceder (relógio acompanha), edição de qualquer tarefa com validação.
- [ ] Modo completo: só o resultado final.
- [ ] Gantt: cor por tarefa, pronta sem cor, suspensa preta pontilhada, CPU indicada, chegada/término no instante exato, legenda, menor ID embaixo.
- [ ] Imagem final (SVG) gerada pelo programa, cobrindo **toda** a simulação, sem limite de tempo.
- [ ] Novo algoritmo adicionável sem mexer no código da simulação.
- [ ] Todo o código comentado com justificativas.
- [ ] Conferido contra os exemplos do Cap. 2: Tabela 2.1 (RM, 1 CPU, escalonável), Figura 2.6 (A=10/20, B=25/50: EDF cumpre, RM perde prazo de B em t=50).
