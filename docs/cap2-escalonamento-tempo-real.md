# Capítulo 2 — O Escalonamento de Tempo Real

> Conversão para Markdown do capítulo 2 de *Sistemas de Tempo Real*.
> Fórmulas reescritas em LaTeX (`$...$`). Figuras (diagramas de Gantt) foram convertidas em descrições textuais / tabelas de linha do tempo.

**Sumário**

- [2.1 Introdução](#21-introdução)
- [2.2 Modelo de Tarefas](#22-modelo-de-tarefas)
- [2.3 Escalonamento de Tempo Real](#23-escalonamento-de-tempo-real)
- [2.4 Escalonamento de Tarefas Periódicas](#24-escalonamento-de-tarefas-periódicas)
- [2.5 Testes de Escalonabilidade em Modelos Estendidos](#25-testes-de-escalonabilidade-em-modelos-estendidos)
- [2.6 Tarefas Dependentes: Compartilhamento de Recursos](#26-tarefas-dependentes-compartilhamento-de-recursos)
- [2.7 Tarefas Dependentes: Relações de Precedência](#27-tarefas-dependentes-relações-de-precedência)
- [2.8 Escalonamento de Tarefas Aperiódicas](#28-escalonamento-de-tarefas-aperiódicas)
- [2.9 Conclusão](#29-conclusão)
- [Resumo de fórmulas](#resumo-de-fórmulas)

---

Em sistemas de tempo real que seguem a abordagem assíncrona, os aspectos de implementação estão presentes mesmo na fase de projeto. Na implementação de restrições temporais, é de fundamental importância o conhecimento das propriedades temporais do suporte de tempo de execução usado e da escolha de uma abordagem de escalonamento de tempo real adequada à classe de problemas que o sistema deve tratar.

Este capítulo trata do escalonamento de tempo real de modo geral. Conceitos, objetivos, hipóteses e métricas são apresentados para introduzir o que chamamos de **problema de escalonamento**. Depois, diferentes classes de problemas de escalonamento são examinadas em suas soluções algorítmicas.

## 2.1 Introdução

Em sistemas onde as noções de tempo e de concorrência são tratadas explicitamente, conceitos e técnicas de escalonamento formam o ponto central na **previsibilidade** do comportamento de sistemas de tempo real. Muitos algoritmos propostos na literatura definem técnicas restritas e de uso limitado em aplicações reais; este capítulo se concentra em técnicas gerais e suas extensões, com perspectiva de uso prático.

O foco é em **escalonamentos dirigidos a prioridades**, porque:

- há ampla literatura sobre eles;
- cobrem diversos comportamentos temporais de aplicações de tempo real;
- a maioria dos suportes comerciais (núcleos, sistemas operacionais) baseia seu escalonamento em prioridades;
- padrões como o **POSIX** enfatizam escalonamento dirigido a prioridades em suas especificações de tempo real. Alguns algoritmos deste capítulo são recomendados pelo POSIX.

## 2.2 Modelo de Tarefas

**Tarefas** (ou processos) são as unidades de processamento sequencial que concorrem sobre um ou mais recursos computacionais. Uma aplicação de tempo real é tipicamente composta de várias tarefas. Uma tarefa de tempo real precisa de:

- **correção lógica** (*correctness*);
- **correção temporal** (*timeliness*): satisfazer seus prazos e restrições temporais.

As restrições temporais, as relações de precedência e as de exclusão impostas sobre as tarefas definem o **modelo de tarefas**, parte integrante do problema de escalonamento.

### 2.2.1 Restrições Temporais

Toda tarefa de tempo real está sujeita a um prazo, o **deadline**. Conforme a consequência de perder o deadline:

- **Tarefas críticas (*hard*)**: completar após o deadline pode causar falhas catastróficas no sistema e em seu ambiente (danos irreversíveis a equipamentos, perda de vidas).
- **Tarefas brandas / não críticas (*soft*)**: completar após o deadline causa no máximo queda de desempenho. Falhas temporais são **benignas**.

Conforme a regularidade de ativação:

- **Tarefas periódicas**: ativações em sequência infinita, uma por intervalo regular chamado **período**. Cada ativação é uma **instância**. Assume-se a primeira ativação em $t = 0$.
- **Tarefas aperiódicas (assíncronas)**: ativações em resposta a eventos internos ou externos, com característica aleatória.
- **Tarefas esporádicas**: subconjunto das aperiódicas com **intervalo mínimo conhecido** entre duas ativações consecutivas ($min_i$). Por isso podem ser críticas (*hard*).

Em geral: periódicas → *hard*; aperiódicas → *soft*; esporádicas → podem ser *hard*.

Outras grandezas temporais de uma tarefa:

| Termo | Significado |
|---|---|
| **Tempo de computação** ($C$, *Computation Time*) | Tempo necessário para a execução completa da tarefa. |
| **Tempo de início** ($st$, *Start Time*) | Instante em que a tarefa começa a executar em uma ativação. |
| **Tempo de término** ($ct$, *Completion Time*) | Instante em que a execução da ativação termina. |
| **Tempo de chegada** ($a$, *Arrival Time*) | Instante em que o escalonador toma conhecimento da ativação. Periódicas: início do período. Aperiódicas: instante da requisição. |
| **Tempo de liberação** ($r$, *Release Time*) | Instante em que a tarefa entra na **fila de Pronto**. |

O tempo de liberação pode não coincidir com o de chegada, por exemplo por causa do *polling* de um escalonador ativado por tempo (*tick scheduler*) ou do bloqueio na recepção de uma mensagem. A máxima variação dos tempos de liberação das instâncias é o **Release Jitter** ($J$).

**Tarefa periódica** $T_i$ é descrita pela quádrupla $(J_i, C_i, P_i, D_i)$:

- $C_i$: tempo de computação;
- $P_i$: período;
- $D_i$: deadline **relativo** (medido a partir do início do período);
- $J_i$: release jitter (pior situação de liberação, também relativo).

Deadline absoluto e liberação (pior caso) da $k$-ésima ativação:

$$d_{ik} = (k-1)P_i + D_i \qquad\qquad r_{ik} = (k-1)P_i + J_i$$

> **Figura 2.1 — Ativações de uma tarefa periódica.** Linha do tempo com três ativações consecutivas, cada uma num período $P_i$. Em cada ativação aparecem: chegada $a$ (início do período), liberação $r$ (deslocada de até $J$ na primeira), início $st$, execução de duração $C_i$, término $ct$ e deadline absoluto $d$ (a distância $D$ do início do período).

**Tarefa esporádica** é descrita pela tripla $(C_i, D_i, min_i)$, onde $D_i$ é medido a partir da chegada da requisição e $min_i$ é o intervalo mínimo entre requisições. Uma **aperiódica pura** é descrita só por $C_i$ e $D_i$. Para a requisição 2 chegando em $a_2$: $d_2 = a_2 + D_i$.

> **Figura 2.2 — Ativações de uma tarefa aperiódica (esporádica).** Duas requisições separadas por pelo menos $min_i$; cada uma com início $st$, execução $C_i$, término $ct$ e deadline $d = a + D_i$.

### 2.2.2 Relações de Precedência e de Exclusão

**Precedência:** $T_i \rightarrow T_j$ significa que $T_j$ só pode iniciar após o término de $T_i$. Pode expressar dependência de dados ou sinais de sincronização. Um conjunto de precedências é representado por um **grafo acíclico orientado** (nós = tarefas, arcos = precedências).

**Exclusão:** $T_i$ exclui $T_j$ quando a seção crítica de $T_j$ que manipula um recurso compartilhado não pode executar porque $T_i$ já ocupa o recurso. Em escalonamento por prioridades, relações de exclusão podem levar a **inversões de prioridade** (tarefas mais prioritárias bloqueadas por menos prioritárias).

## 2.3 Escalonamento de Tempo Real

### 2.3.1 Principais Conceitos

- **Escalonamento (*scheduling*)**: procedimento de ordenar tarefas na fila de Pronto.
- **Escala de execução (*schedule*)**: ordenação que indica a ordem de ocupação do processador.
- **Escalonador (*scheduler*)**: componente que, em tempo de execução, gerencia o processador implementando uma **política de escalonamento**.
- **Escala realizável (*feasible*)**: garante o cumprimento das restrições temporais.
- **Escala ótima**: a melhor possível segundo os critérios da política.

Classificações dos algoritmos:

| Critério | Classes |
|---|---|
| Interrupção | **Preemptivo**: tarefa em execução pode ser interrompida por outra mais prioritária. **Não preemptivo**: não pode. |
| Parâmetros | **Estático**: escala baseada em parâmetros fixos definidos em projeto. **Dinâmico**: parâmetros mudam em execução. |
| Momento do cálculo | **Off-line**: escala produzida em tempo de projeto. **On-line**: escala produzida em tempo de execução. |

Combinações possíveis: off-line estático, on-line estático, on-line dinâmico.

Um problema de escalonamento geral envolve processadores, recursos compartilhados e tarefas com restrições temporais, de precedência e de exclusão. Na forma geral é **NP-completo**. Os algoritmos existentes resolvem em tempo polinomial casos particulares (com hipóteses simplificadoras); sem simplificações, usam-se **heurísticas** que encontram escalas realizáveis mas não necessariamente ótimas.

**Algoritmo ótimo:** minimiza algum custo/métrica da sua classe. Sem métrica definida: é ótimo se encontra uma escala realizável sempre que algum outro algoritmo da mesma classe encontrar. Ou seja, se o ótimo falha, todos da classe falham.

### 2.3.2 Abordagens de Escalonamento

A **carga computacional** (*task load*) é a soma dos tempos de computação das tarefas na fila de Pronto.

- **Carga estática (limitada)**: todas as tarefas e seus tempos de chegada são conhecidos em projeto; o pior caso (pico) também. Modelada com tarefas **periódicas e esporádicas**.
- **Carga dinâmica (ilimitada)**: chegadas não podem ser antecipadas. Modelada com tarefas **aperiódicas** (basta uma aperiódica com intervalo mínimo nulo para o pico ser desconhecido).

O escalonamento costuma ter duas etapas: (1) **teste de escalonabilidade**; (2) **cálculo da escala**.

Taxonomia de abordagens:

```
Escalonamento de Tempo Real
├── Garantia em tempo de projeto (off-line guarantee)
│   ├── Executivo Cíclico
│   └── Dirigido a Prioridades
├── Garantia dinâmica (on-line guarantee)
└── Melhor esforço (best-effort)
    └── Técnicas adaptativas
```
*(Figura 2.3 — Abordagens de escalonamento de tempo real)*

**Garantia em tempo de projeto** — previsibilidade determinista. Premissas:
- carga conhecida em projeto (estática);
- recursos suficientes para atender as restrições no pior caso.

Permite testar a escalonabilidade e até redimensionar o sistema em projeto. Adequada para aplicações críticas com carga conhecida (embarcados, tráfego ferroviário, controle de processos).

- **Executivo cíclico**: teste e escala feitos em projeto. A escala (grade) é finita e define a ocupação dos *slots* do processador; o teste está implícito na montagem da grade. Em execução, o escalonador é um simples **despachante** que segue a grade ciclicamente. Como a grade reflete o pior caso (piores chegadas, piores $C_i$, esporádicas na frequência máxima), há **desperdício de recursos** no caso médio.
- **Dirigido a prioridades**: teste em projeto, escala produzida on-line por um escalonador de prioridades. Prioridades fixas (on-line estático) ou variáveis (on-line dinâmico). O pior caso aparece só no teste, sem reserva de recursos em execução — **mais flexível**.

**Garantia dinâmica** — carga dinâmica. Um **teste de aceitação** é executado em tempo de execução sobre o conjunto {nova tarefa + tarefas já na fila}. Se falhar, a nova tarefa é **descartada**, preservando as já garantidas. Para aplicações críticas em ambientes não deterministas (sistemas militares, radar, controle aéreo).

**Melhor esforço** — tenta encontrar uma escala em execução sem testes (ou com testes fracos). Sem garantia. Adequado para tarefas *soft* (ex.: multimídia). No caso médio tem desempenho melhor que as abordagens com garantia (que podem descartar tarefas por pessimismo); tarefas só são abortadas em sobrecarga real.

**Técnicas adaptativas** para sobrecarga (usadas em melhor esforço): Computação Imprecisa, *Deadline (m,k) Firm*, Tarefas com Duplo Deadline.

Este capítulo se concentra em **escalonamento dirigido a prioridades**.

### 2.3.3 Teste de Escalonabilidade

Determina se existe escala realizável para um conjunto de tarefas. Normalmente são análises de pior caso.

| Tipo | Garante | Pode errar |
|---|---|---|
| **Exato** | Aceita todos os escalonáveis e rejeita todos os não escalonáveis | Não erra, mas muitas vezes é impraticável |
| **Suficiente** | Quem passa **é** escalonável | Pode rejeitar conjuntos escalonáveis (mais restritivo) |
| **Necessário** | Quem é rejeitado **não é** escalonável | Pode aceitar conjuntos não escalonáveis |

*(Figura 2.4 — conjuntos aceitos: suficiente ⊂ exato ⊂ necessário, à medida que a complexidade dos conjuntos aumenta.)*

**Utilização de uma tarefa:**

$$U_i = \frac{C_i}{P_i} \;\text{(periódica)} \qquad\qquad U_i = \frac{C_i}{min_i} \;\text{(esporádica)}$$

**Utilização do processador** para $\{T_1, \dots, T_n\}$:

$$U = \sum_{i=1}^{n} U_i \qquad [1]$$

Teste básico de utilização com $m$ processadores:

$$U = \sum_{i=1}^{n} U_i \le m$$

Testes baseados em utilização podem ser exatos, necessários ou suficientes dependendo da política e do modelo de tarefas.

## 2.4 Escalonamento de Tarefas Periódicas

Tarefas periódicas têm chegadas conhecidas a priori, o que permite garantias em projeto. Aqui são tratadas em esquemas dirigidos a prioridades, onde **as prioridades derivam das restrições temporais** (não da importância da tarefa).

Algoritmos clássicos:
- **Rate Monotonic (RM)** — prioridade fixa;
- **Deadline Monotonic (DM)** — prioridade fixa;
- **Earliest Deadline First (EDF)** — prioridade dinâmica.

Cada um é ótimo na sua classe de problema.

### 2.4.1 Escalonamento Taxa Monotônica (Rate Monotonic — RM)

- Preemptivo, dirigido a prioridades, **prioridade fixa** → estático e on-line.
- **Ótimo entre os de prioridade fixa**: se o RM não escalona um conjunto, nenhum outro algoritmo de prioridade fixa escalona.

**Premissas:**
1. Tarefas periódicas e independentes.
2. Deadline igual ao período ($D_i = P_i$).
3. $C_i$ conhecido e constante (*Worst Case Computation Time*).
4. Tempo de chaveamento entre tarefas nulo.

**Política:** prioridade decresce com o aumento do período — **quanto menor o período (mais frequente), maior a prioridade**.

**Teste (condição suficiente):**

$$U = \sum_{i=1}^{n} \frac{C_i}{P_i} \le n\left(2^{1/n} - 1\right) \qquad [2]$$

Quando $n \to \infty$, o limite converge para $\ln 2 \approx 0{,}69$. Isso é conservador: muitos conjuntos com utilização maior são escalonáveis mas são rejeitados.

Se os períodos forem **múltiplos do período da tarefa mais prioritária** (harmônicos), o teste vira necessário e suficiente:

$$U = \sum_{i=1}^{n} \frac{C_i}{P_i} \le 1$$

**Exemplo — Tabela 2.1**

| Tarefa | $P_i$ | $C_i$ | Prioridade RM | $U_i$ |
|---|---|---|---|---|
| A | 100 | 20 | 1 | 0,200 |
| B | 150 | 40 | 2 | 0,267 |
| C | 350 | 100 | 3 | 0,286 |

$$U = 0{,}753 \le 3\left(2^{1/3} - 1\right) = 0{,}779 \;\Rightarrow\; \text{escalonável pelo RM}$$

> **Figura 2.5 — Escala RM da Tabela 2.1** (A, B e C chegam em $t=0$)
>
> | Intervalo | Executa | Evento |
> |---|---|---|
> | 0–20 | A | A conclui |
> | 20–60 | B | B conclui |
> | 60–100 | C | A chega em 100 e preempta C |
> | 100–120 | A | |
> | 120–150 | C | B chega em 150 e preempta C |
> | 150–190 | B | |
> | 190–200 | C | A chega em 200 e preempta C |
> | 200–220 | A | |
> | 220–240 | C | C conclui (total 100) |

### 2.4.2 Earliest Deadline First (EDF)

- Preemptivo, **prioridade dinâmica** → on-line e dinâmico.
- **Ótimo entre os de prioridade dinâmica**.
- Mesmas premissas do RM (periódicas independentes, $D_i = P_i$, $C_i$ constante, chaveamento nulo).

**Política:** a tarefa mais prioritária é a que tem o **deadline absoluto mais próximo**. A cada chegada a fila de prontos é reordenada. Deadline da $k$-ésima ativação: $d_{ik} = kP_i$.

**Teste (necessário e suficiente):**

$$U = \sum_{i=1}^{n} \frac{C_i}{P_i} \le 1 \qquad [3]$$

Se alguma premissa é relaxada (ex.: $D_i \ne P_i$), [3] continua necessária mas deixa de ser suficiente.

**Exemplo — Figura 2.6** ($U = 10/20 + 25/50 = 1{,}0$)

| Tarefa | $C_i$ | $P_i$ | $D_i$ |
|---|---|---|---|
| A | 10 | 20 | 20 |
| B | 25 | 50 | 50 |

Pela [2] não é escalonável por RM; pela [3] é escalonável por EDF.

> **(a) EDF:** 0–10 A · 10–20 B · 20–30 A (deadline 40 < 50) · 30–45 B (conclui antes de 50) · 45–55 A …  → todos os deadlines cumpridos.
>
> **(b) RM:** 0–10 A · 10–20 B · 20–30 A · 30–40 B · 40–50 A → em $t=50$ B executou só 20 de 25: **B perde o deadline**.

Comparação: EDF permite utilização maior e normalmente gera **menos preempções**; RM é **mais simples de implementar**.

### 2.4.3 Escalonamento Deadline Monotônico (DM)

Estende o RM permitindo **deadline menor ou igual ao período** ($D_i \le P_i$). Demais premissas iguais (periódicas independentes, $C_i$ de pior caso, chaveamento nulo).

**Política:** prioridade fixa em ordem inversa ao deadline relativo — **menor $D_i$, maior prioridade**. Preemptivo, estático, on-line, **ótimo na sua classe**.

**Exemplo — Figura 2.7**

| Tarefa | $C_i$ | $P_i$ | $D_i$ | $p_i$ |
|---|---|---|---|---|
| A | 2 | 10 | 6 | 1 |
| B | 2 | 10 | 8 | 2 |
| C | 8 | 20 | 16 | 3 |

> Escala DM: 0–2 A · 2–4 B · 4–10 C · 10–12 A · 12–14 B · 14–16 C (C conclui exatamente no deadline 16).

O DM foi proposto sem teste; um teste necessário e suficiente baseado em **tempo de resposta** aparece na seção 2.5.2.

## 2.5 Testes de Escalonabilidade em Modelos Estendidos

Ao estender os modelos (precedência, deadlines arbitrários etc.), os testes por utilização passam a ser apenas **necessários**, e são desejáveis testes mais precisos.

Tarefas esporádicas podem ser incluídas interpretando $P_i$ como $min_i$: garantir a esporádica na frequência máxima garante as ativações menos frequentes.

Os testes consideram o pior caso: tarefas com seu $C_i$ máximo, esporádicas na frequência máxima e o **instante crítico** — o instante em que **todas as tarefas ficam prontas ao mesmo tempo**. Se o conjunto atende as restrições nesse cenário, atende em qualquer outro.

Esta seção trata de testes para **prioridade fixa** (os de prioridade dinâmica estão no Anexo A do livro).

### 2.5.1 Deadline Igual ao Período

Teste com utilização em função de uma janela de tempo $t$. A **carga cumulativa** (*workload*) das tarefas de prioridade maior ou igual a $i$ no intervalo $t$:

$$W_i(t) = \sum_{j=1}^{i} \left\lceil \frac{t}{P_j} \right\rceil C_j \qquad [4]$$

$\lceil t/P_j \rceil$ é o número máximo de ativações de $T_j$ em $t$.

Utilização no intervalo:

$$U_i(t) = \frac{W_i(t)}{t} \qquad [5]$$

**Condição necessária e suficiente:** $T_i$ é escalonável se existe $t$ com $U_i(t) \le 1$, ou seja, $W_i(t) \le t$. Como o instante crítico é $t=0$, basta verificar o primeiro deadline, então $t \in (0, P_i]$:

$$\forall i, \quad \min_{0 < t \le P_i} U_i(t) \le 1$$

Em vez de testar todos os $t$, basta testar os pontos de chegada das tarefas de prioridade $\ge i$ dentro de $P_i$ (os mínimos de $U_i(t)$):

$$S_i = \left\{ k P_j \;\middle|\; j = 1, \dots, i;\; k = 1, \dots, \left\lfloor \frac{P_i}{P_j} \right\rfloor \right\} \qquad [6]$$

Conjunto escalonável pelo RM se:

$$\forall i, \quad \min_{t \in S_i} U_i(t) \le 1 \qquad [7]$$

Mais complexo que [2], mas aceita conjuntos que [2] rejeitaria (necessário e suficiente).

**Exemplo — Tabela 2.2** (mesmo conjunto da Figura 2.6)

| Tarefa | $C_i$ | $P_i$ | $D_i$ |
|---|---|---|---|
| T1 | 10 | 20 | 20 |
| T2 | 25 | 50 | 50 |

$$W_1(t) = \left\lceil \frac{t}{20} \right\rceil 10 \qquad W_2(t) = \left\lceil \frac{t}{20} \right\rceil 10 + \left\lceil \frac{t}{50} \right\rceil 25$$

- T1: $S_1 = \{20\}$ → $U_1(20) = 10/20 = 0{,}5 \le 1$ → **escalonável**.
- T2: $S_2 = \{20, 40, 50\}$:
  - $U_2(20) = (10 + 25)/20 = 1{,}75$
  - $U_2(40) = (20 + 25)/40 = 1{,}125$
  - $U_2(50) = (30 + 25)/50 = 1{,}1$

  O mínimo é $1{,}1 > 1$ → **T2 não escalonável**; o conjunto não é escalonável pelo RM.

### 2.5.2 Deadline Menor que o Período

Modelo com $D_i \le P_i$, baseado em **tempo de resposta máximo**: tempo entre a chegada e o término, considerando a máxima interferência de tarefas de prioridade maior.

$$R_i = C_i + \sum_{j \in hp(i)} I_j \qquad I_j = \left\lceil \frac{R_i}{P_j} \right\rceil C_j$$

onde $hp(i)$ é o conjunto de tarefas com prioridade maior que $i$, e $\lceil R_i/P_j \rceil$ é o número de liberações de $T_j$ dentro de $R_i$. Logo:

$$R_i = C_i + \sum_{j \in hp(i)} \left\lceil \frac{R_i}{P_j} \right\rceil C_j \qquad [8]$$

Como $R_i$ aparece dos dois lados, resolve-se **iterativamente**:

$$R_i^{n+1} = C_i + \sum_{j \in hp(i)} \left\lceil \frac{R_i^{n}}{P_j} \right\rceil C_j, \qquad R_i^0 = C_i$$

Para quando $R_i^{n+1} = R_i^n$. Não converge se $U > 100\%$.

**Teste (necessário e suficiente):** $\forall i,\; R_i \le D_i$.

**Exemplo — Tabela 2.3** (conjunto da Figura 2.7, prioridades DM: $p_A > p_B > p_C$)

| Tarefa | $C_i$ | $P_i$ | $D_i$ |
|---|---|---|---|
| A | 2 | 10 | 6 |
| B | 2 | 10 | 8 |
| C | 8 | 20 | 16 |

- **A**: sem interferência → $R_A = C_A = 2 \le 6$ ✓
- **B**:
  - $R_B^0 = 2$
  - $R_B^1 = 2 + \lceil 2/10 \rceil \cdot 2 = 4$
  - $R_B^2 = 2 + \lceil 4/10 \rceil \cdot 2 = 4$ → $R_B = 4 \le 8$ ✓
- **C**:
  - $R_C^0 = 8$
  - $R_C^1 = 8 + \lceil 8/10 \rceil 2 + \lceil 8/10 \rceil 2 = 12$
  - $R_C^2 = 8 + \lceil 12/10 \rceil 2 + \lceil 12/10 \rceil 2 = 16$
  - $R_C^3 = 8 + \lceil 16/10 \rceil 2 + \lceil 16/10 \rceil 2 = 16$ → $R_C = 16 \le 16$ ✓ (no limite)

**Com release jitter.** Escalonadores ativados por tempo (*tick scheduler*) atrasam a liberação; esse atraso no pior caso é o jitter $J$. Introduz-se o **período ocupado** (*busy period*): o *i-busy period* é a janela $W_i$ de execução contínua de tarefas com prioridade $\ge i$, começando na liberação de $T_i$ com todas as mais prioritárias prontas.

Uma instância de $T_j$ anterior a $W_i$ e atrasada por $J_j$ pode interferir, então o número de ativações interferentes é $\lceil (W_i + J_j)/P_j \rceil$:

$$W_i = C_i + \sum_{j \in hp(i)} \left\lceil \frac{W_i + J_j}{P_j} \right\rceil C_j \qquad [9]$$

$W_i$ vai da liberação ao término. O tempo de resposta (da chegada ao término) soma o jitter de $T_i$:

$$R_i = W_i + J_i \qquad [10]$$

Teste: [9], [10] e $\forall i,\; R_i \le D_i$.

### 2.5.3 Deadline Arbitrário

Com $D_i > P_i$, o teste anterior deixa de ser suficiente: ativações anteriores da **mesma** tarefa podem interferir na seguinte (**interferência interna**). Assume-se que uma liberação só executa após as anteriores da mesma tarefa terminarem.

O *i-busy period* agora começa na liberação de uma ativação anterior de $T_i$ e pode conter $q+1$ ativações de $T_i$:

$$W_i(q) = (q+1)\,C_i + \sum_{j \in hp(i)} \left\lceil \frac{W_i(q)}{P_j} \right\rceil C_j \qquad [11]$$

$q \cdot C_i$ representa a interferência interna sofrida pela última instância (ex.: $q=2$ → três liberações de $T_i$ na janela).

Tempo de resposta da $(q+1)$-ésima ativação:

$$R_i(q) = W_i(q) - q\,P_i \qquad [12]$$

Inspecionam-se $q = 0, 1, 2, \dots$ até que:

$$W_i(q) \le (q+1)\,P_i$$

(maior *busy period* antes de executar uma tarefa menos prioritária). Então:

$$R_i = \max_{q = 0,1,2,\dots} R_i(q)$$

**Com jitter e deadline arbitrário:**

$$W_i(q) = (q+1)\,C_i + \sum_{j \in hp(i)} \left\lceil \frac{W_i(q) + J_j}{P_j} \right\rceil C_j \qquad [13]$$

$$R_i = \max_{q = 0,1,2,\dots} \left( J_i + W_i(q) - q\,P_i \right) \qquad [14]$$

Teste: [13], [14] e $\forall i,\; R_i \le D_i$. Vale para **qualquer política de prioridade fixa**. Complexidade pseudo-polinomial (na prática, tratável como polinomial).

**Exemplo — Tabela 2.4** (prioridades $p_1 > p_2 > p_3$)

| Tarefa | $J_i$ | $C_i$ | $P_i$ | $D_i$ |
|---|---|---|---|---|
| T1 | 1 | 10 | 40 | 40 |
| T2 | 3 | 10 | 80 | 25 |
| T3 | – | 5 | 20 | 40 |

$$W_3(q) = (q+1)\,5 + \left\lceil \frac{W_3(q) + 1}{40} \right\rceil 10 + \left\lceil \frac{W_3(q) + 3}{80} \right\rceil 10$$

**q = 0:**
- $W^0 = 5$
- $W^1 = 5 + \lceil 6/40 \rceil 10 + \lceil 8/80 \rceil 10 = 25$
- $W^2 = 5 + \lceil 26/40 \rceil 10 + \lceil 28/80 \rceil 10 = 25$

$W_3(0) = 25$, $R_3(0) = 25$. Como $25 > P_3 = 20$, a janela não é a maior → continuar.

**q = 1:**
- $W^0 = 10$ (ponto de partida)
- $W^1 = 10 + \lceil 11/40 \rceil 10 + \lceil 13/80 \rceil 10 = 30$
- $W^2 = 10 + \lceil 31/40 \rceil 10 + \lceil 33/80 \rceil 10 = 30$

$W_3(1) = 30$, $R_3(1) = 30 - 20 = 10$. Como $30 \le 2 \cdot 20 = 40$, este é o maior *3-busy period* (duas ativações de T3).

$$R_3 = \max(25, 10) = 25 \le D_3 = 40 \;✓$$

> **Figura 2.8 — Maior período ocupado de T3.** T1 e T2 chegam em $t=-1$ e $t=-3$ mas, pelos jitters, só são liberadas em $t=0$. T3 é liberada em $t=0$ e $t=20$. Escala: 0–10 T2 · 10–20 T1 · 20–25 T3 (1ª ativação, termina em 25, fora do seu período) · 25–30 T3 (2ª ativação). A primeira ativação de T3 é empurrada para fora do período e interfere na seguinte.

## 2.6 Tarefas Dependentes: Compartilhamento de Recursos

Na maioria das aplicações as tarefas **não** são independentes: compartilham recursos (variáveis compartilhadas protegidas por semáforos, monitores etc.), gerando relações de exclusão.

**Inversão de prioridade:** tarefa mais prioritária bloqueada por uma menos prioritária.

> **Figura 2.9 — Inversão de prioridades.** Quatro tarefas RM com $p_1 > p_2 > p_3 > p_4$. T1 e T4 compartilham um recurso. T4 entra na seção crítica (SC); T1 é liberada, pede a SC e bloqueia. Enquanto isso T2 e T3 (prioridades intermediárias) preemptam T4, prolongando o bloqueio de T1 por um tempo **difícil de determinar** (inversão não limitada).

Com tarefas dependentes, a inversão de prioridade é inevitável; o objetivo é que ela seja **limitada** e conhecida a priori. Protocolos (para prioridade fixa): **Herança de Prioridade** e **Prioridade Teto**. (A *Stack Resource Policy*, para prioridade dinâmica, está no Anexo A do livro.)

### 2.6.1 Protocolo Herança de Prioridade (PHP)

Solução simples alternativa: desabilitar preempção dentro de seções críticas. Evita interferências intermediárias, mas penaliza tarefas mais prioritárias que nem usam o recurso, se as SCs forem longas.

**Ideia do PHP:** quando uma tarefa menos prioritária bloqueia uma mais prioritária num recurso, a menos prioritária **herda** a prioridade da bloqueada.

**Descrição:**
- Cada tarefa tem uma **prioridade nominal** (estática, definida por RM, DM etc.) e uma **prioridade ativa** (dinâmica). Sem bloqueios, as duas coincidem. O escalonamento usa a **ativa**.
- Quando $T_i$ bloqueia num semáforo mantido por $T_j$, $T_j$ passa a executar com a prioridade de $T_i$ ($p_j = p_i$). Uma tarefa executa sempre com a **maior** prioridade entre as tarefas que mantém bloqueadas.
- Ao liberar o semáforo, $T_j$ volta à prioridade nominal (ou à maior prioridade entre as tarefas que ainda bloqueia).

> **Figura 2.10 — PHP aplicado ao exemplo da Figura 2.9.** T1 bloqueia em $t=5$ (bloqueio direto); T4 herda a prioridade de T1. T2 e T3 chegam em $t=6$ e não conseguem preemptar T4 (bloqueio por herança). Em $t=7$ T4 sai da SC e volta à prioridade original; T1 executa.

**Tipos de bloqueio no PHP:**
- **Direto**: a tarefa mais prioritária tenta acessar um recurso já bloqueado pela menos prioritária.
- **Por herança**: uma tarefa intermediária é impedida de executar por uma tarefa que herdou prioridade de uma mais prioritária.
- **Transitivo** (com SCs aninhadas): cadeia de bloqueios. Na **Figura 2.11**, T1 é bloqueada por T2, que é bloqueada por T3, que é bloqueada por T4 → T4 herda a prioridade de T1. T1 só retoma após T4, T3 e T2 liberarem suas SCs. Cadeias podem levar até a **deadlocks**.

**Limite de bloqueio:** se $T_i$ pode ser bloqueada por $n$ tarefas menos prioritárias e por $m$ semáforos distintos, então pode ser bloqueada no máximo pela duração de $\min(n, m)$ seções críticas.

**Testes com bloqueio ($B_i$ = bloqueio máximo de $T_i$).** Calcular $B_i$ com precisão é difícil; usam-se estimativas. Extensão do teste RM:

$$\forall i, \quad \sum_{j=1}^{i} \frac{C_j}{P_j} + \frac{B_i}{P_i} \le i\left(2^{1/i} - 1\right) \qquad [15]$$

$B_i/P_i$ é a utilização perdida por bloqueio. As $n$ condições precisam ser verificadas.

**Exemplo — Tabela 2.5**

| Tarefa | $C_i$ | $P_i$ | $B_i$ |
|---|---|---|---|
| T1 | 6 | 18 | 2 |
| T2 | 4 | 20 | 4 |
| T3 | 10 | 50 | 0 |

$$\frac{C_1}{P_1} + \frac{B_1}{P_1} = \frac{6}{18} + \frac{2}{18} = 0{,}444 \le 1$$
$$\frac{C_1}{P_1} + \frac{C_2}{P_2} + \frac{B_2}{P_2} = 0{,}333 + 0{,}2 + 0{,}2 = 0{,}733 \le 0{,}828$$
$$\frac{C_1}{P_1} + \frac{C_2}{P_2} + \frac{C_3}{P_3} = 0{,}333 + 0{,}2 + 0{,}2 = 0{,}733 \le 0{,}780$$

Todas se verificam → **escalonável**.

Variante com uma única equação (mais simples, porém **mais restritiva**):

$$\sum_{i=1}^{n} \frac{C_i}{P_i} + \max\left(\frac{B_1}{P_1}, \dots, \frac{B_{n-1}}{P_{n-1}}\right) \le n\left(2^{1/n} - 1\right) \qquad [16]$$

Para a Tabela 2.5: $0{,}733 + \max(0{,}111;\; 0{,}2) = 0{,}933 > 0{,}780$ → rejeitado. Como [16] é mais restritivo, todo conjunto rejeitado por ele deve ser conferido com [15]; o que passa em [16] certamente é escalonável.

Extensão do teste por utilização em janela (2.5.1):

$$U_i(t) = \sum_{j=1}^{i} \left\lceil \frac{t}{P_j} \right\rceil \frac{C_j}{t} + \frac{B_i}{t}, \qquad \forall i, \min_{0 < t \le P_i} U_i(t) \le 1 \qquad [17]$$

Extensão do teste por tempo de resposta (deadline arbitrário + jitter):

$$W_i(q) = (q+1)\,C_i + B_i + \sum_{j \in hp(i)} \left\lceil \frac{W_i(q) + J_j}{P_j} \right\rceil C_j \qquad [18]$$

Com bloqueio, esses testes deixam de ser exatos e passam a ser **suficientes**, porque o cálculo de $B_i$ é pessimista.

### 2.6.2 Protocolo de Prioridade Teto (Priority Ceiling Protocol — PCP)

Objetivo: limitar bloqueios e **evitar cadeias de bloqueio e deadlocks**. É o PHP mais uma regra de controle de entrada em seções críticas. Para prioridade fixa (ex.: RM).

**Garantia principal:** no máximo **uma** inversão de prioridade por ativação — uma tarefa só pode ser bloqueada por tarefas menos prioritárias **uma vez** por ativação.

**Descrição:**
- Cada tarefa tem prioridade nominal (RM) e ativa (com herança, como no PHP). A herança é **transitiva**: se T3 bloqueia T2 e T2 bloqueia T1, T3 herda a prioridade de T1.
- Cada recurso/semáforo $S_k$ tem uma **prioridade teto** $C(S_k)$ = prioridade da tarefa mais prioritária que o acessa.
- **Regra de acesso:** uma tarefa só entra numa SC se sua prioridade ativa for **maior** que o teto de todos os semáforos atualmente bloqueados por **outras** tarefas. Sendo $S_l$ o semáforo bloqueado de maior teto, $T_i$ entra só se $p_i > C(S_l)$; se $p_i \le C(S_l)$, o acesso é negado.
- Se nenhum recurso estiver bloqueado, o acesso é sempre permitido.

**Bloqueio de teto (*ceiling blocking*):** além dos bloqueios direto e por herança, uma tarefa pode ser bloqueada por não ter prioridade maior que o maior teto entre os recursos ocupados. Isso é o que evita cadeias e deadlocks.

**Exemplo — Figura 2.12** (prioridades $p_1 > p_2 > p_3$, numericamente $p_1 = 1, p_2 = 2, p_3 = 3$; tetos $C(S_1) = 1$, $C(S_2) = 1$, $C(S_3) = 2$)

| Evento | Descrição |
|---|---|
| 1 | T3 começa a executar. |
| 2 | T3 entra em SC (fecha S3). |
| 3 | T2 inicia e preempta T3. |
| 4 | T2 sofre bloqueio direto (S3 fechado por T3). T3 reassume e herda a prioridade de T2. |
| 5 | T3 entra em SC aninhada (fecha S2). |
| 6 | T1 inicia e preempta T3 (T1 é mais prioritária que T3 mesmo com a herança de T2). |
| 7 | T1 tenta fechar S1 e sofre **bloqueio de teto**: sua prioridade é igual ao maior teto dos semáforos fechados por outros ($C(S_2) = 1$). |
| 8 | T3 libera S2. T1 é reativada, preempta T3 e entra em S1. |
| 9 | T1 libera S1. |
| 10 | T1 fecha S2. |
| 11 | T1 libera S2. |
| 12 | T1 termina; T3 reassume com a prioridade herdada de T2. |
| 13 | T3 libera S3 e volta à prioridade estática. T2 preempta T3 e fecha S3. |
| 14–17 | T2 libera S3; fecha e libera S1; completa. T3 reassume e completa. |

T1 é bloqueada no máximo uma vez por ativação por uma SC de tarefa menos prioritária.

**Immediate Priority Ceiling Protocol (IPCP):** variante com melhor desempenho. A tarefa assume a **prioridade teto do recurso logo ao entrar na SC** (não espera bloquear alguém). Consequências:
- uma tarefa só pode ser bloqueada **no início** da sua execução;
- depois que começa, tem todos os recursos que precisa; só sofre interferência de tarefas mais prioritárias;
- mais fácil de implementar que o PCP e com menos trocas de contexto.

O *Priority Protect Protocol* do POSIX é baseado no IPCP.

**Testes com PCP.** Os mesmos testes do PHP valem; muda o $B_i$: no PCP, $B_i$ é a duração da **maior seção crítica** de tarefa menos prioritária que pode bloquear $T_i$.

Uma SC de $T_j$, guardada por $S_k$ e com duração $D_{j,k}$, pode bloquear $T_i$ se e somente se $p_i > p_j$ e $C(S_k) \ge p_i$. Então:

$$B_i = \max_{j,k} \left\{ D_{j,k} \;\middle|\; p_j < p_i \;\wedge\; C(S_k) \ge p_i \right\}$$

**Exemplo — Tabela 2.6** (durações das SCs da Figura 2.12)

| Tarefa | S1 | S2 | S3 |
|---|---|---|---|
| T1 | 1 | 1 | 0 |
| T2 | 1 | 0 | 1 |
| T3 | 0 | 4 | 8 |

- $B_1 = \max(1, 4) = 4$
- $B_2 = \max(8) = 8$
- $B_3 = 0$

## 2.7 Tarefas Dependentes: Relações de Precedência

Algumas tarefas precisam executar em ordem definida (ordem parcial). Duas formas de implementar a liberação da sucessora:

- **Por offset (passagem de tempo):** a sucessora é liberada após um offset fixo que garante o pior tempo de resposta da predecessora. Técnica **estática**; impõe sempre o pior caso → pode subutilizar recursos.
- **Por mensagem:** a predecessora envia mensagem/sinal ao terminar e libera a sucessora. Técnica **dinâmica**; gera **release jitter** na sucessora.

Para análise, ambos são equivalentes: offset ou jitter devem valer o pior tempo de resposta da predecessora.

**Atividade:** entidade que encapsula tarefas que se comunicam/sincronizam, representada por um grafo acíclico orientado.
- **Atividade síncrona** (*loosely synchronous*): liberação por mensagem.
- **Atividade assíncrona**: liberação por offset (ex.: transações no sistema MARS).

Aqui: atividades síncronas, prioridade fixa, precedência tratada como **jitter**. Cada atividade periódica $A_i$ tem período $P_i$ e deadline $D_i$ (limite para concluir todas as suas tarefas). As tarefas de uma atividade têm a mesma chegada, mas sua liberação depende do tempo de resposta da predecessora.

$T_i \rightarrow T_j$: $T_j$ não pode iniciar antes de $T_i$ terminar. A relação é transitiva.

**Atribuição de prioridades:** decrescente ao longo do grafo, seguindo os arcos (próximo do DM). Isso também reduz bloqueios, pois as mais prioritárias são liberadas antes.

**Exemplo — Figura 2.13** (atividade 1: T1; atividade 2: T2 → T3 → T4; índice menor = maior prioridade)

| Tarefa | $J_i$ | $C_i$ | $P_i$ | $D_i$ |
|---|---|---|---|---|
| T1 | 1 | 10 | 40 | 40 |
| T2 | 3 | 10 | 80 | 25 |
| T3 | – | 5 | 80 | 40 |
| T4 | – | 10 | 80 | 80 |

Usam-se [9] e [10]:

$$W_i = C_i + \sum_{j \in hp(i)} \left\lceil \frac{W_i + J_j}{P_j} \right\rceil C_j, \qquad R_i = W_i + J_i, \qquad \forall i,\; R_i \le D_i$$

O jitter de uma sucessora é o tempo de resposta máximo da predecessora.

Observação: tratar as tarefas como independentes com jitter dá tempos de resposta **pessimistas**. Na prática, T2 não interfere em T3 e T4, pois elas só são liberadas depois que T2 termina; sua influência é só via jitter.

- **T1**: $R_1 = C_1 + J_1 = 10 + 1 = 11 \le 40$ ✓
- **T2** (interferência de T1):
  - $W^0 = 10$
  - $W^1 = 10 + \lceil (10+1)/40 \rceil 10 = 20$
  - $W^2 = 10 + \lceil (20+1)/40 \rceil 10 = 20$
  - $R_2 = 20 + 3 = 23 \le 25$ ✓
- **T3** (interferência de T1; jitter $J_3 = R_2 = 23$):
  - $W^0 = 5$
  - $W^1 = 5 + \lceil (5+1)/40 \rceil 10 = 15$
  - $W^2 = 5 + \lceil (15+1)/40 \rceil 10 = 15$
  - $R_3 = 15 + 23 = 38 \le 40$ ✓
- **T4** (interferência de T1 e T3; T3 tem jitter 23; $J_4 = R_2 = 23$):
  - $W^0 = 10$
  - $W^1 = 10 + \lceil (10+1)/40 \rceil 10 + \lceil (10+23)/80 \rceil 5 = 25$
  - $W^2 = 10 + \lceil (25+1)/40 \rceil 10 + \lceil (25+23)/80 \rceil 5 = 25$
  - $R_4 = 25 + 23 = 48 \le 80$ ✓

Todas escalonáveis.

Tempos de comunicação local podem ser somados ao $C_i$ das predecessoras (emissoras). Em sistemas **distribuídos**, usa-se a **análise holística** (*holistic schedulability analysis*): o jitter de uma mensagem depende do pior tempo de resposta da tarefa emissora, e o pior tempo de resposta da tarefa receptora depende do tempo de resposta das mensagens.

## 2.8 Escalonamento de Tarefas Aperiódicas

Cenário **misto**: periódicas (críticas, garantidas em projeto) + aperiódicas.

Tipos de aperiódicas:
- **Esporádicas**: intervalo mínimo entre ativações e deadline *hard* → comportamento determinista, garantia em projeto.
- **Firm**: aperiódicas com deadline *hard* que precisam de **garantia dinâmica** a cada ativação.
- **Soft** ou sem requisitos temporais: só precisam de **bons tempos de resposta**.

Prioridade fixa (RM, DM) sempre foi a preferida para esquemas mistos. EDF tem maior limite de escalonabilidade (sobra mais processador para a carga aperiódica) e passou a ser estendido também para esquemas mistos.

As **sobras de processador** são usadas para as aperiódicas. Duas abordagens:
- **Servidores** (tratada aqui);
- **Roubo de folga** (*slack stealing*).

### 2.8.1 Servidores de Prioridade Fixa

Baseados no RM. As sobras da carga periódica são determinadas em projeto e, em execução, cedidas ao processamento aperiódico via um **servidor**.

#### Servidor de Background (BS)

Atende aperiódicas **só quando não há periódicas prontas**. Periódicas recebem as prioridades mais altas (RM); aperiódicas as mais baixas.

- Muito simples.
- Tempos de resposta **muito altos** para aperiódicas, especialmente com carga periódica alta.
- Aplicável só quando as aperiódicas não são críticas e a carga periódica não é alta.

**Exemplo — Figura 2.14**

| Tarefa | $C_i$ | $P_i$ | $D_i$ | $p_i$ |
|---|---|---|---|---|
| A (periódica) | 4 | 10 | 10 | 1 |
| B (periódica) | 8 | 20 | 20 | 2 |
| C (aperiódica) | 1 | – | – | 3 |
| D (aperiódica) | 1 | – | – | 3 |

A carga periódica ($U = 0{,}8$) passa no teste RM ($0{,}828$ para $n=2$). C e D só executam no fim, depois que a carga periódica do intervalo foi concluída.

#### Polling Server (PS)

Cria uma **tarefa periódica servidora** com período $P_{PS}$ e capacidade $C_{PS}$, com prioridade atribuída pelo RM. A cada ativação, executa as aperiódicas pendentes até o limite de $C_{PS}$.

- Se **não há aperiódicas pendentes**, a servidora se suspende até o próximo período e **perde a capacidade** desse período (ela vai para as periódicas).
- Uma aperiódica que chega logo após a suspensão espera o próximo período.

**Exemplo — Figura 2.15** ($C_{PS} = 1$, $P_{PS} = 5$, PS com prioridade mais alta)

| Tarefa | $C_i$ | $P_i$ | $D_i$ | $p_i$ |
|---|---|---|---|---|
| A (periódica) | 4 | 10 | 10 | 3 |
| B (periódica) | 8 | 20 | 20 | 2 |
| PS (servidora) | 1 | 5 | – | 1 |
| C (aperiódica) | 1 | – | – | – |
| D (aperiódica) | 0,5 | – | – | – |

- $t=0$: sem aperiódicas → capacidade cedida às periódicas.
- $t=5$: C chega junto com a servidora → consome toda a capacidade até $t=6$.
- $t=10$: sem aperiódicas → capacidade cedida.
- $t=12$: D chega, mas a servidora está suspensa → espera.
- $t=15$: D executa, consumindo 0,5 da capacidade.

**Escalonabilidade:** a servidora se comporta, no pior caso, como uma tarefa periódica $(C_{PS}, P_{PS})$:

$$\sum_{i=1}^{n} \frac{C_i}{P_i} + \frac{C_{PS}}{P_{PS}} \le (n+1)\left(2^{1/(n+1)} - 1\right)$$

ou seja, $U_P + U_{PS} \le (n+1)\left(2^{1/(n+1)} - 1\right)$.

Melhora o tempo de resposta médio em relação ao BS, mas **não oferece resposta imediata**: depende do período e da capacidade da servidora.

#### Deferrable Server (DS)

Também é uma tarefa periódica com prioridade RM, mas **conserva a capacidade** mesmo sem requisições. Aperiódicas são atendidas no nível de prioridade da servidora enquanto houver capacidade $C_{DS}$ no período; a capacidade é **restaurada no início de cada período**.

- Melhores tempos de resposta que o PS.
- Com a servidora na prioridade mais alta e capacidade suficiente, o atendimento é **imediato**.

**Exemplo — Figura 2.16** ($C_{DS} = 1$, $P_{DS} = 5$; mesmas tarefas da Figura 2.15)

- $t=0$: sem aperiódicas → capacidade **preservada** durante o período.
- $t=5$: C chega → consome toda a capacidade até $t=6$.
- $t=10$: capacidade restaurada para 1 e mantida.
- $t=12$: D chega → executa **imediatamente**, consumindo 0,5 até $t=12{,}5$.
- $t=15$: capacidade volta a 1.

**Escalonabilidade:** o DS pode executar em qualquer ponto do período, o que o teste RM não capta (no RM, a tarefa mais prioritária precisa executar na chegada). Relação entre a utilização da carga periódica $U_P$ e a da servidora $U_{DS}$:

$$U_P \le \ln\left(\frac{U_{DS} + 2}{2\,U_{DS} + 1}\right) \qquad [19]$$

Válida apenas para um número muito grande de tarefas periódicas.

#### Priority Exchange Server (PE)

Sem requisições aperiódicas, a capacidade $C_{PE}$ é preservada **trocando prioridade** com tarefas periódicas pendentes. Não é detalhado no texto devido à complexidade do mecanismo de troca e à pouca aplicação prática.

#### Sporadic Server (SS)

Bons tempos de resposta e serviço imediato (como DS e PE), e foi criado para permitir **aperiódicas com restrições críticas**. A servidora atua em um único nível de prioridade.

Termos:
- $p_s$: nível de prioridade em execução no processador;
- $p_i$: um dos níveis de prioridade do sistema;
- **Intervalo ativo**: $p_i$ está ativa quando $p_i \le p_s$ (ou seja, a prioridade em execução é igual ou maior);
- **Intervalo de prioridade desativada**: $p_i > p_s$;
- **Tempo de preenchimento** $RT_i$: instante em que a capacidade consumida durante o intervalo ativo de $p_i$ é restaurada.

Preserva a capacidade (como o DS), mas o **preenchimento** é diferente:
- A capacidade consumida num intervalo ativo é reposta em $RT_i = (\text{início do intervalo ativo em que houve consumo}) + P_{SS}$.
- A quantidade reposta é **igual à consumida** naquele intervalo ativo.

**Exemplo — Figura 2.17** ($C_{SS} = 2{,}5$, $P_{SS} = 10$, SS com prioridade média)

| Tarefa | $C_i$ | $P_i$ | $D_i$ | $p_i$ |
|---|---|---|---|---|
| A (periódica) | 1 | 5 | 5 | 1 |
| B (periódica) | 6 | 14 | 14 | 3 |
| SS (servidora) | 2,5 | 10 | – | 2 |
| C (aperiódica) | 1 | – | – | – |
| D (aperiódica) | 1 | – | – | – |

- $t=0$: A (mais prioritária) executa → intervalo ativo da SS, mas sem consumo (sem aperiódicas).
- $t=4{,}5$: C chega; SS é mais prioritária que B (em execução) → preempta B. C consome capacidade até $t=5$.
- $t=5$: A chega e preempta C. Após A, C reassume e termina em $t=6{,}5$.
- O intervalo ativo vai de $4{,}5$ a $6{,}5$ (a preempção por A não o divide, pois $p_{SS} \le p_s$ continua valendo). Reposição em $RT = 4{,}5 + 10 = 14{,}5$.
- D chega em $t=8$ num novo intervalo ativo, preempta B e consome 1 unidade. Reposição em $RT = 8 + 10 = 18$.

**Escalonabilidade:** apesar do comportamento não convencional, prova-se que o esquema de reposição permite tratar a SS **como uma tarefa periódica comum** $(C_{SS}, P_{SS})$ no teste RM [2]. Limite imposto sobre a carga periódica:

$$U_P \le \ln\left(\frac{2}{U_{SS} + 1}\right) \qquad [20]$$

Idêntica à do PE; válida só para muitas tarefas periódicas.

**Tabela 2.7 — Utilização dos servidores DS, PE e SS para a mesma carga periódica**

| Tarefa | $C_i$ | $P_i$ | $U_i$ (%) |
|---|---|---|---|
| tarefa 1 | 2 | 10 | 20,0 |
| tarefa 2 | 6 | 14 | 42,9 |
| servidor DS | 1,00 | 5 | 20,0 |
| servidor PE | 1,33 | 5 | 26,7 |
| servidor SS | 1,33 | 5 | 26,7 |

O SS tem a **simplicidade do DS** e a **maior capacidade do PE**, e ainda pode ser usado em **garantia em tempo de projeto**.

**SS para tarefas esporádicas (deadline *hard*):** cria-se uma SS exclusiva para a esporádica, que assume os deadlines dela, com:
- $P_{SS} \le min_i$ (intervalo mínimo da esporádica);
- $C_{SS}$ suficiente para o $C_i$ da esporádica em cada ativação.

Assim os deadlines críticos ficam garantidos em projeto.

- Se $D_i = min_i$: prioridades da SS e das periódicas pelo **RM**.
- Se $D_i < min_i$: o RM não serve; é preciso política dirigida a deadline, como o **DM**, que pode dar à SS a maior prioridade.

> **Figura 2.18 — SS com RM, carga aperiódica com $D_i < min_i$.** A(4,12,12), B(4,20,20), SS($C=8$, $P=32$, $D=10$) e aperiódica C ($C=8$, $D=10$, $min=32$). Pelo RM, a SS tem o maior período → menor prioridade (3). Todas chegam em $t=0$: A executa 0–4, B 4–8, C só começa em 8 e **perde o deadline em $t=10$**.
>
> **Figura 2.19 — Mesmo conjunto com DM.** A SS tem o menor deadline (10) → maior prioridade (1); A → 2; B → 3. C executa 0–8 (cumpre o deadline 10), A 8–12, B 12–16. A reposição da capacidade de C ocorre em $t=32$, antes de esgotar $min_C$. Todos os deadlines cumpridos.

### 2.8.2 Considerações sobre as Técnicas de Servidores

- Os servidores podem ser usados também com periódicas de **deadlines arbitrários** e **recursos compartilhados**; a análise deve considerar o modelo usado.
- Aqui as aperiódicas foram tratadas sem deadline, em **melhor esforço com FIFO**. Elas podem ter restrições temporais e ser ordenadas por outras políticas.
- Aperiódicas **firm** exigem **teste de aceitação** a cada chegada; se falhar, a tarefa é descartada. PS, PE e DS são apropriados para verificação dinâmica de tarefas firm.
- O **SS** permite tratar **esporádicas** com garantia em tempo de projeto para deadlines *hard*.

## 2.9 Conclusão

O capítulo focou em escalonamento **dirigido a prioridades**, por cobrir muitos comportamentos temporais e ter literatura ampla. Foram vistos, sempre com prioridade fixa:

- escalonamento de periódicas, incluindo deadlines arbitrários;
- compartilhamento de recursos (PHP, PCP, IPCP);
- relações de precedência (via jitter);
- aperiódicas com servidores (BS, PS, DS, PE, SS).

Escalonamentos de prioridade dinâmica (como o EDF) permitem maior utilização, mas têm **maior complexidade em tempo de execução**. A grande difusão de núcleos e sistemas operacionais baseados em prioridade, e as recomendações do POSIX e da OMG, justificam o uso prático dessas técnicas.

---

## Resumo de fórmulas

| # | Uso | Fórmula |
|---|---|---|
| [1] | Utilização do processador | $U = \sum_{i=1}^{n} C_i/P_i$ |
| [2] | RM (suficiente) | $\sum C_i/P_i \le n(2^{1/n} - 1)$ |
| — | RM com períodos harmônicos (exato) | $\sum C_i/P_i \le 1$ |
| [3] | EDF (exato, $D_i = P_i$) | $\sum C_i/P_i \le 1$ |
| [4] | Workload | $W_i(t) = \sum_{j=1}^{i} \lceil t/P_j \rceil C_j$ |
| [5] | Utilização na janela | $U_i(t) = W_i(t)/t$ |
| [6] | Pontos de teste | $S_i = \{kP_j \mid j \le i,\; k = 1..\lfloor P_i/P_j \rfloor\}$ |
| [7] | RM exato | $\forall i,\; \min_{t \in S_i} U_i(t) \le 1$ |
| [8] | Tempo de resposta ($D_i \le P_i$) | $R_i = C_i + \sum_{j \in hp(i)} \lceil R_i/P_j \rceil C_j$ |
| [9] | Busy period com jitter | $W_i = C_i + \sum_{j \in hp(i)} \lceil (W_i + J_j)/P_j \rceil C_j$ |
| [10] | Tempo de resposta com jitter | $R_i = W_i + J_i$ |
| [11] | Deadline arbitrário | $W_i(q) = (q+1)C_i + \sum_{j \in hp(i)} \lceil W_i(q)/P_j \rceil C_j$ |
| [12] | Resposta da $(q+1)$-ésima ativação | $R_i(q) = W_i(q) - qP_i$ (parar quando $W_i(q) \le (q+1)P_i$) |
| [13] | Deadline arbitrário + jitter | $W_i(q) = (q+1)C_i + \sum_{j \in hp(i)} \lceil (W_i(q) + J_j)/P_j \rceil C_j$ |
| [14] | Resposta máxima | $R_i = \max_q (J_i + W_i(q) - qP_i)$ |
| [15] | RM + bloqueio | $\forall i,\; \sum_{j=1}^{i} C_j/P_j + B_i/P_i \le i(2^{1/i} - 1)$ |
| [16] | RM + bloqueio (uma equação) | $\sum C_i/P_i + \max(B_1/P_1, \dots, B_{n-1}/P_{n-1}) \le n(2^{1/n} - 1)$ |
| [17] | Utilização em janela + bloqueio | $U_i(t) = \sum_{j=1}^{i} \lceil t/P_j \rceil C_j/t + B_i/t$ |
| [18] | Resposta + bloqueio | $W_i(q) = (q+1)C_i + B_i + \sum_{j \in hp(i)} \lceil (W_i(q) + J_j)/P_j \rceil C_j$ |
| — | Bloqueio máximo no PCP | $B_i = \max\{D_{j,k} \mid p_j < p_i \wedge C(S_k) \ge p_i\}$ |
| — | Polling Server | $U_P + C_{PS}/P_{PS} \le (n+1)(2^{1/(n+1)} - 1)$ |
| [19] | Deferrable Server | $U_P \le \ln\left(\frac{U_{DS} + 2}{2U_{DS} + 1}\right)$ |
| [20] | Sporadic Server / PE | $U_P \le \ln\left(\frac{2}{U_{SS} + 1}\right)$ |

### Comparativo rápido dos algoritmos

| Algoritmo | Prioridade | Critério | Premissa de deadline | Teste |
|---|---|---|---|---|
| RM | Fixa | Menor período → maior prioridade | $D_i = P_i$ | $U \le n(2^{1/n}-1)$ (suficiente) ou [7] (exato) |
| DM | Fixa | Menor deadline relativo → maior prioridade | $D_i \le P_i$ | Tempo de resposta [8] (exato) |
| EDF | Dinâmica | Deadline absoluto mais próximo → maior prioridade | $D_i = P_i$ | $U \le 1$ (exato) |

### Comparativo rápido dos servidores aperiódicos

| Servidor | Capacidade sem requisições | Resposta imediata | Observação |
|---|---|---|---|
| Background | — (usa só tempo ocioso) | Não | Mais simples; resposta ruim |
| Polling | Perdida até o próximo período | Não | Analisado como tarefa periódica comum |
| Deferrable | Preservada; reposta no início de cada período | Sim | Teste RM não se aplica diretamente → [19] |
| Priority Exchange | Preservada via troca de prioridade | Sim | Complexo; pouco usado |
| Sporadic | Preservada; reposta $P_{SS}$ após o início do intervalo ativo de consumo | Sim | Tratado como tarefa periódica no teste RM; garante esporádicas *hard* |
