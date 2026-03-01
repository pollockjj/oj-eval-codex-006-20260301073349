#ifndef CLIENT_H
#define CLIENT_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern int rows;         // The count of rows of the game map.
extern int columns;      // The count of columns of the game map.
extern int total_mines;  // The count of mines of the game map.

// You MUST NOT use any other external variables except for rows, columns and total_mines.

struct Action {
  int r = -1;
  int c = -1;
  int type = 0;
  bool valid = false;
};

struct Constraint {
  std::vector<int> vars;
  int mines = 0;
};

std::vector<std::string> known_map;

bool InBoundsClient(int r, int c) {
  return r >= 0 && r < rows && c >= 0 && c < columns;
}

int ToKey(int r, int c) {
  return r * columns + c;
}

std::pair<int, int> FromKey(int key) {
  return {key / columns, key % columns};
}

bool IsNumberCell(char ch) {
  return ch >= '0' && ch <= '8';
}

bool IsSubsetSorted(const std::vector<int> &a, const std::vector<int> &b) {
  int i = 0;
  int j = 0;
  while (i < static_cast<int>(a.size()) && j < static_cast<int>(b.size())) {
    if (a[i] == b[j]) {
      ++i;
      ++j;
    } else if (a[i] > b[j]) {
      ++j;
    } else {
      return false;
    }
  }
  return i == static_cast<int>(a.size());
}

std::vector<int> SortedDiff(const std::vector<int> &big, const std::vector<int> &small) {
  std::vector<int> diff;
  int i = 0;
  int j = 0;
  while (i < static_cast<int>(big.size())) {
    if (j >= static_cast<int>(small.size()) || big[i] < small[j]) {
      diff.push_back(big[i]);
      ++i;
    } else if (big[i] == small[j]) {
      ++i;
      ++j;
    } else {
      ++j;
    }
  }
  return diff;
}

Action FindSimpleDeterministicAction() {
  Action auto_action;
  Action mark_action;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < columns; ++c) {
      if (!IsNumberCell(known_map[r][c])) {
        continue;
      }
      int number = known_map[r][c] - '0';
      int marked = 0;
      std::vector<std::pair<int, int>> unknown;
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          int nr = r + dx;
          int nc = c + dy;
          if (!InBoundsClient(nr, nc)) {
            continue;
          }
          if (known_map[nr][nc] == '@') {
            ++marked;
          } else if (known_map[nr][nc] == '?') {
            unknown.push_back({nr, nc});
          }
        }
      }
      if (unknown.empty()) {
        continue;
      }
      int remaining = number - marked;
      if (remaining == 0) {
        auto_action = {r, c, 2, true};
      } else if (remaining == static_cast<int>(unknown.size()) && !mark_action.valid) {
        mark_action = {unknown[0].first, unknown[0].second, 1, true};
      }
    }
  }
  if (auto_action.valid) {
    return auto_action;
  }
  if (mark_action.valid) {
    return mark_action;
  }
  return Action{};
}

Action BuildAndSolveFrontier() {
  std::vector<std::pair<int, int>> frontier_cells;
  std::unordered_map<int, int> key_to_id;
  std::vector<Constraint> constraints;
  std::vector<std::vector<int>> cell_to_constraints;
  std::vector<double> mine_probability;

  int marked_count = 0;
  int unknown_count = 0;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < columns; ++c) {
      if (known_map[r][c] == '@') {
        ++marked_count;
      } else if (known_map[r][c] == '?') {
        ++unknown_count;
      }
    }
  }

  auto get_id = [&](int r, int c) -> int {
    int key = ToKey(r, c);
    auto it = key_to_id.find(key);
    if (it != key_to_id.end()) {
      return it->second;
    }
    int id = static_cast<int>(frontier_cells.size());
    frontier_cells.push_back({r, c});
    key_to_id[key] = id;
    return id;
  };

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < columns; ++c) {
      if (!IsNumberCell(known_map[r][c])) {
        continue;
      }
      int number = known_map[r][c] - '0';
      int marked = 0;
      std::vector<int> vars;
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          int nr = r + dx;
          int nc = c + dy;
          if (!InBoundsClient(nr, nc)) {
            continue;
          }
          if (known_map[nr][nc] == '@') {
            ++marked;
          } else if (known_map[nr][nc] == '?') {
            vars.push_back(get_id(nr, nc));
          }
        }
      }
      if (vars.empty()) {
        continue;
      }
      std::sort(vars.begin(), vars.end());
      vars.erase(std::unique(vars.begin(), vars.end()), vars.end());
      int remaining = number - marked;
      if (remaining < 0 || remaining > static_cast<int>(vars.size())) {
        continue;
      }
      constraints.push_back({vars, remaining});
    }
  }

  if (frontier_cells.empty()) {
    return Action{};
  }

  cell_to_constraints.assign(frontier_cells.size(), {});
  for (int cid = 0; cid < static_cast<int>(constraints.size()); ++cid) {
    for (int v : constraints[cid].vars) {
      cell_to_constraints[v].push_back(cid);
    }
  }

  std::vector<char> forced_mine(frontier_cells.size(), 0);
  std::vector<char> forced_safe(frontier_cells.size(), 0);

  for (int i = 0; i < static_cast<int>(constraints.size()); ++i) {
    for (int j = 0; j < static_cast<int>(constraints.size()); ++j) {
      if (i == j) {
        continue;
      }
      const auto &a = constraints[i];
      const auto &b = constraints[j];
      if (!IsSubsetSorted(a.vars, b.vars)) {
        continue;
      }
      auto diff = SortedDiff(b.vars, a.vars);
      if (diff.empty()) {
        continue;
      }
      int remain = b.mines - a.mines;
      if (remain == 0) {
        for (int v : diff) {
          forced_safe[v] = 1;
        }
      } else if (remain == static_cast<int>(diff.size())) {
        for (int v : diff) {
          forced_mine[v] = 1;
        }
      }
    }
  }

  constexpr int kMaxExactCells = 22;
  constexpr int kMaxApproxCells = 34;
  constexpr long long kApproxNodeLimit = 250000;
  constexpr long long kApproxSolutionLimit = 8000;
  mine_probability.assign(frontier_cells.size(), std::numeric_limits<double>::quiet_NaN());

  struct ComponentModel {
    std::vector<int> global_cells;
    std::vector<long double> ways_by_mines;
    std::vector<std::vector<long double>> mine_ways_by_cell;
  };

  std::vector<ComponentModel> component_models;
  std::vector<int> frontier_component(frontier_cells.size(), -1);
  std::vector<int> frontier_local(frontier_cells.size(), -1);

  std::vector<char> visited_cell(frontier_cells.size(), 0);
  std::vector<char> visited_constraint(constraints.size(), 0);

  for (int start = 0; start < static_cast<int>(frontier_cells.size()); ++start) {
    if (visited_cell[start] || cell_to_constraints[start].empty()) {
      continue;
    }
    std::vector<int> comp_cells;
    std::vector<int> comp_constraints;
    std::queue<int> q;
    visited_cell[start] = 1;
    q.push(start);
    while (!q.empty()) {
      int u = q.front();
      q.pop();
      comp_cells.push_back(u);
      for (int cid : cell_to_constraints[u]) {
        if (!visited_constraint[cid]) {
          visited_constraint[cid] = 1;
          comp_constraints.push_back(cid);
          for (int v : constraints[cid].vars) {
            if (!visited_cell[v]) {
              visited_cell[v] = 1;
              q.push(v);
            }
          }
        }
      }
    }

    if (static_cast<int>(comp_cells.size()) > kMaxApproxCells) {
      continue;
    }

    std::unordered_map<int, int> global_to_local;
    for (int i = 0; i < static_cast<int>(comp_cells.size()); ++i) {
      global_to_local[comp_cells[i]] = i;
    }

    struct LocalConstraint {
      std::vector<int> vars;
      int mines;
    };
    std::vector<LocalConstraint> local_constraints;
    local_constraints.reserve(comp_constraints.size());
    for (int cid : comp_constraints) {
      std::vector<int> vars;
      vars.reserve(constraints[cid].vars.size());
      for (int v : constraints[cid].vars) {
        vars.push_back(global_to_local[v]);
      }
      local_constraints.push_back({vars, constraints[cid].mines});
    }

    int n = static_cast<int>(comp_cells.size());
    int m = static_cast<int>(local_constraints.size());
    std::vector<std::vector<int>> local_incident(n);
    for (int cid = 0; cid < m; ++cid) {
      for (int v : local_constraints[cid].vars) {
        local_incident[v].push_back(cid);
      }
    }

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return local_incident[a].size() > local_incident[b].size();
    });

    if (n <= kMaxExactCells) {
      std::vector<int> assigned_mines(m, 0);
      std::vector<int> unassigned(m, 0);
      std::vector<int> assign(n, -1);
      std::vector<long double> solutions_by_mines(n + 1, 0.0L);
      std::vector<std::vector<long double>> mine_solutions_by_mines(n, std::vector<long double>(n + 1, 0.0L));
      long double total_solutions = 0.0L;
      int assigned_mine_total = 0;

      for (int cid = 0; cid < m; ++cid) {
        unassigned[cid] = static_cast<int>(local_constraints[cid].vars.size());
      }

      std::function<void(int)> dfs = [&](int idx) {
        if (idx == n) {
          total_solutions += 1.0L;
          solutions_by_mines[assigned_mine_total] += 1.0L;
          for (int v = 0; v < n; ++v) {
            if (assign[v] == 1) {
              mine_solutions_by_mines[v][assigned_mine_total] += 1.0L;
            }
          }
          return;
        }
        int v = order[idx];
        for (int val = 0; val <= 1; ++val) {
          bool ok = true;
          for (int cid : local_incident[v]) {
            unassigned[cid]--;
            assigned_mines[cid] += val;
            int need = local_constraints[cid].mines;
            if (assigned_mines[cid] > need || assigned_mines[cid] + unassigned[cid] < need) {
              ok = false;
            }
          }
          assign[v] = val;
          assigned_mine_total += val;
          if (ok) {
            dfs(idx + 1);
          }
          assigned_mine_total -= val;
          assign[v] = -1;
          for (int cid : local_incident[v]) {
            assigned_mines[cid] -= val;
            unassigned[cid]++;
          }
        }
      };
      dfs(0);

      if (total_solutions == 0.0L) {
        continue;
      }

      int comp_index = static_cast<int>(component_models.size());
      ComponentModel model;
      model.global_cells = comp_cells;
      model.ways_by_mines = solutions_by_mines;
      model.mine_ways_by_cell = mine_solutions_by_mines;
      component_models.push_back(model);

      for (int local = 0; local < n; ++local) {
        int global = comp_cells[local];
        frontier_component[global] = comp_index;
        frontier_local[global] = local;
        long double mine_sum = 0.0L;
        for (int k = 0; k <= n; ++k) {
          mine_sum += mine_solutions_by_mines[local][k];
        }
        if (mine_sum == 0.0L) {
          forced_safe[global] = 1;
          mine_probability[global] = 0.0;
        } else if (mine_sum == total_solutions) {
          forced_mine[global] = 1;
          mine_probability[global] = 1.0;
        } else {
          mine_probability[global] = static_cast<double>(mine_sum / total_solutions);
        }
      }
      continue;
    }

    std::vector<int> assigned_mines(m, 0);
    std::vector<int> unassigned(m, 0);
    std::vector<int> assign(n, -1);
    std::vector<long double> sampled_mines(n, 0.0L);
    long double sampled_solutions = 0.0L;
    int assigned_mine_total = 0;
    long long visited_nodes = 0;
    bool truncated = false;

    for (int cid = 0; cid < m; ++cid) {
      unassigned[cid] = static_cast<int>(local_constraints[cid].vars.size());
    }

    std::function<void(int)> dfs_approx = [&](int idx) {
      if (truncated) {
        return;
      }
      if (++visited_nodes > kApproxNodeLimit) {
        truncated = true;
        return;
      }
      if (idx == n) {
        sampled_solutions += 1.0L;
        for (int v = 0; v < n; ++v) {
          if (assign[v] == 1) {
            sampled_mines[v] += 1.0L;
          }
        }
        if (sampled_solutions >= static_cast<long double>(kApproxSolutionLimit)) {
          truncated = true;
        }
        return;
      }

      int v = order[idx];
      int first_val = ((idx + n + assigned_mine_total) & 1);
      for (int t = 0; t < 2; ++t) {
        int val = (t == 0) ? first_val : (1 - first_val);
        bool ok = true;
        for (int cid : local_incident[v]) {
          unassigned[cid]--;
          assigned_mines[cid] += val;
          int need = local_constraints[cid].mines;
          if (assigned_mines[cid] > need || assigned_mines[cid] + unassigned[cid] < need) {
            ok = false;
          }
        }
        assign[v] = val;
        assigned_mine_total += val;
        if (ok) {
          dfs_approx(idx + 1);
        }
        assigned_mine_total -= val;
        assign[v] = -1;
        for (int cid : local_incident[v]) {
          assigned_mines[cid] -= val;
          unassigned[cid]++;
        }
        if (truncated) {
          break;
        }
      }
    };

    dfs_approx(0);
    if (sampled_solutions == 0.0L) {
      continue;
    }
    for (int local = 0; local < n; ++local) {
      int global = comp_cells[local];
      double prob = static_cast<double>(sampled_mines[local] / sampled_solutions);
      mine_probability[global] = prob;
      if (!truncated) {
        if (sampled_mines[local] == 0.0L) {
          forced_safe[global] = 1;
        } else if (sampled_mines[local] == sampled_solutions) {
          forced_mine[global] = 1;
        }
      }
    }
  }

  int remaining_mines = total_mines - marked_count;
  if (remaining_mines < 0) {
    remaining_mines = 0;
  }
  if (remaining_mines > unknown_count) {
    remaining_mines = unknown_count;
  }

  int unresolved_frontier = 0;
  for (int id = 0; id < static_cast<int>(frontier_cells.size()); ++id) {
    if (frontier_component[id] == -1) {
      ++unresolved_frontier;
    }
  }
  int unconstrained_unknown = unknown_count - static_cast<int>(frontier_cells.size());

  double global_prob = unknown_count > 0 ? static_cast<double>(remaining_mines) / static_cast<double>(unknown_count) : 1.0;
  global_prob = std::clamp(global_prob, 0.0, 1.0);

  if (unresolved_frontier == 0 && !component_models.empty()) {
    std::vector<long double> comb(unconstrained_unknown + 1, 0.0L);
    comb[0] = 1.0L;
    for (int i = 1; i <= unconstrained_unknown; ++i) {
      comb[i] = comb[i - 1] * static_cast<long double>(unconstrained_unknown - i + 1) / static_cast<long double>(i);
    }

    int comp_count = static_cast<int>(component_models.size());
    std::vector<std::vector<long double>> prefix(comp_count + 1, std::vector<long double>(remaining_mines + 1, 0.0L));
    std::vector<std::vector<long double>> suffix(comp_count + 1, std::vector<long double>(remaining_mines + 1, 0.0L));
    prefix[0][0] = 1.0L;
    for (int i = 0; i < comp_count; ++i) {
      for (int used = 0; used <= remaining_mines; ++used) {
        if (prefix[i][used] == 0.0L) {
          continue;
        }
        for (int k = 0; k < static_cast<int>(component_models[i].ways_by_mines.size()); ++k) {
          if (component_models[i].ways_by_mines[k] == 0.0L || used + k > remaining_mines) {
            continue;
          }
          prefix[i + 1][used + k] += prefix[i][used] * component_models[i].ways_by_mines[k];
        }
      }
    }
    suffix[comp_count][0] = 1.0L;
    for (int i = comp_count - 1; i >= 0; --i) {
      for (int used = 0; used <= remaining_mines; ++used) {
        if (suffix[i + 1][used] == 0.0L) {
          continue;
        }
        for (int k = 0; k < static_cast<int>(component_models[i].ways_by_mines.size()); ++k) {
          if (component_models[i].ways_by_mines[k] == 0.0L || used + k > remaining_mines) {
            continue;
          }
          suffix[i][used + k] += suffix[i + 1][used] * component_models[i].ways_by_mines[k];
        }
      }
    }

    long double denominator = 0.0L;
    for (int used = 0; used <= remaining_mines; ++used) {
      int rest = remaining_mines - used;
      if (rest < 0 || rest > unconstrained_unknown) {
        continue;
      }
      denominator += prefix[comp_count][used] * comb[rest];
    }

    if (denominator > 0.0L) {
      if (unconstrained_unknown > 0) {
        long double unconstrained_numerator = 0.0L;
        for (int used = 0; used <= remaining_mines; ++used) {
          int rest = remaining_mines - used;
          if (rest < 0 || rest > unconstrained_unknown) {
            continue;
          }
          unconstrained_numerator += prefix[comp_count][used] * comb[rest] *
                                     static_cast<long double>(rest) / static_cast<long double>(unconstrained_unknown);
        }
        global_prob = static_cast<double>(unconstrained_numerator / denominator);
      } else {
        global_prob = 1.0;
      }

      for (int ci = 0; ci < comp_count; ++ci) {
        std::vector<long double> other_ways(remaining_mines + 1, 0.0L);
        for (int left = 0; left <= remaining_mines; ++left) {
          if (prefix[ci][left] == 0.0L) {
            continue;
          }
          for (int right = 0; right + left <= remaining_mines; ++right) {
            if (suffix[ci + 1][right] == 0.0L) {
              continue;
            }
            other_ways[left + right] += prefix[ci][left] * suffix[ci + 1][right];
          }
        }

        const auto &model = component_models[ci];
        int n = static_cast<int>(model.global_cells.size());
        for (int local = 0; local < n; ++local) {
          long double numerator = 0.0L;
          for (int k = 0; k < static_cast<int>(model.mine_ways_by_cell[local].size()); ++k) {
            if (model.mine_ways_by_cell[local][k] == 0.0L || k > remaining_mines) {
              continue;
            }
            for (int other = 0; other + k <= remaining_mines; ++other) {
              if (other_ways[other] == 0.0L) {
                continue;
              }
              int rest = remaining_mines - other - k;
              if (rest < 0 || rest > unconstrained_unknown) {
                continue;
              }
              numerator += model.mine_ways_by_cell[local][k] * other_ways[other] * comb[rest];
            }
          }
          mine_probability[model.global_cells[local]] = static_cast<double>(numerator / denominator);
        }
      }
    }
  }

  for (int id = 0; id < static_cast<int>(frontier_cells.size()); ++id) {
    if (!std::isnan(mine_probability[id])) {
      continue;
    }
    double worst_prob = -1.0;
    int r = frontier_cells[id].first;
    int c = frontier_cells[id].second;
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        if (dx == 0 && dy == 0) {
          continue;
        }
        int nr = r + dx;
        int nc = c + dy;
        if (!InBoundsClient(nr, nc) || !IsNumberCell(known_map[nr][nc])) {
          continue;
        }
        int number = known_map[nr][nc] - '0';
        int marked = 0;
        int unknown = 0;
        for (int ddx = -1; ddx <= 1; ++ddx) {
          for (int ddy = -1; ddy <= 1; ++ddy) {
            if (ddx == 0 && ddy == 0) {
              continue;
            }
            int rr = nr + ddx;
            int cc = nc + ddy;
            if (!InBoundsClient(rr, cc)) {
              continue;
            }
            if (known_map[rr][cc] == '@') {
              ++marked;
            } else if (known_map[rr][cc] == '?') {
              ++unknown;
            }
          }
        }
        if (unknown == 0) {
          continue;
        }
        int rem = number - marked;
        if (rem < 0) {
          continue;
        }
        double p = static_cast<double>(rem) / static_cast<double>(unknown);
        if (p > worst_prob) {
          worst_prob = p;
        }
      }
    }
    if (worst_prob >= 0.0) {
      mine_probability[id] = worst_prob;
    }
  }

  for (int id = 0; id < static_cast<int>(frontier_cells.size()); ++id) {
    if (forced_safe[id] && !forced_mine[id]) {
      return {frontier_cells[id].first, frontier_cells[id].second, 0, true};
    }
  }
  for (int id = 0; id < static_cast<int>(frontier_cells.size()); ++id) {
    if (forced_mine[id] && !forced_safe[id]) {
      return {frontier_cells[id].first, frontier_cells[id].second, 1, true};
    }
  }

  int best_r = -1;
  int best_c = -1;
  double best_prob = 2.0;
  int best_info = -1;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < columns; ++c) {
      if (known_map[r][c] != '?') {
        continue;
      }
      double p = global_prob;
      auto it = key_to_id.find(ToKey(r, c));
      if (it != key_to_id.end() && !std::isnan(mine_probability[it->second])) {
        p = mine_probability[it->second];
      }
      int info = 0;
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          int nr = r + dx;
          int nc = c + dy;
          if (InBoundsClient(nr, nc) && IsNumberCell(known_map[nr][nc])) {
            ++info;
          }
        }
      }
      if (p < best_prob || (std::abs(p - best_prob) < 1e-12 && info > best_info)) {
        best_prob = p;
        best_info = info;
        best_r = r;
        best_c = c;
      }
    }
  }

  if (best_r != -1) {
    return {best_r, best_c, 0, true};
  }
  return Action{};
}

/**
 * @brief The definition of function Execute(int, int, bool)
 *
 * @details This function is designed to take a step when player the client's (or player's) role, and the implementation
 * of it has been finished by TA. (I hope my comments in code would be easy to understand T_T) If you do not understand
 * the contents, please ask TA for help immediately!!!
 *
 * @param r The row coordinate (0-based) of the block to be visited.
 * @param c The column coordinate (0-based) of the block to be visited.
 * @param type The type of operation to a certain block.
 * If type == 0, we'll execute VisitBlock(row, column).
 * If type == 1, we'll execute MarkMine(row, column).
 * If type == 2, we'll execute AutoExplore(row, column).
 * You should not call this function with other type values.
 */
void Execute(int r, int c, int type);

/**
 * @brief The definition of function InitGame()
 *
 * @details This function is designed to initialize the game. It should be called at the beginning of the game, which
 * will read the scale of the game map and the first step taken by the server (see README).
 */
void InitGame() {
  known_map.assign(rows, std::string(columns, '?'));
  int first_row, first_column;
  std::cin >> first_row >> first_column;
  Execute(first_row, first_column, 0);
}

/**
 * @brief The definition of function ReadMap()
 *
 * @details This function is designed to read the game map from stdin when playing the client's (or player's) role.
 * Since the client (or player) can only get the limited information of the game map, so if there is a 3 * 3 map as
 * above and only the block (2, 0) has been visited, the stdin would be
 *     ???
 *     12?
 *     01?
 */
void ReadMap() {
  for (int i = 0; i < rows; ++i) {
    std::string line;
    std::cin >> line;
    while (static_cast<int>(line.size()) < columns) {
      std::string extra;
      std::cin >> extra;
      line += extra;
    }
    known_map[i] = line.substr(0, columns);
  }
}

/**
 * @brief The definition of function Decide()
 *
 * @details This function is designed to decide the next step when playing the client's (or player's) role. Open up your
 * mind and make your decision here! Caution: you can only execute once in this function.
 */
void Decide() {
  Action action = FindSimpleDeterministicAction();
  if (!action.valid) {
    action = BuildAndSolveFrontier();
  }
  if (!action.valid) {
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < columns; ++c) {
        if (known_map[r][c] == '?') {
          action = {r, c, 0, true};
          break;
        }
      }
      if (action.valid) {
        break;
      }
    }
  }
  if (!action.valid) {
    action = {0, 0, 2, true};
  }
  Execute(action.r, action.c, action.type);
}

#endif
