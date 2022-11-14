#include "cbComputeGraph.hpp"
#include <workflow/WFGraphTask.h>

namespace graph {

cbGraphSharedMem::~cbGraphSharedMem() {
  for (auto item : m_dataFromDevice) { delete item; }
  for (auto item : m_dataPool) { delete item; }
}

void cbGraphSharedMem::push(cbVirtualSharedTable* v) { m_dataFromDevice.push_back(v); }

void cbGraphSharedMem::push(cbMySQLCell* v) { m_dataPool.push_back(v); }

size_t cbGraphSharedMem::getMemUsed() {
  size_t tmp = sizeof(cbMySQLCell) * m_dataPool.size();
  for (auto& item : m_dataFromDevice) {
    auto __shape = item->getShape();
    tmp += sizeof(cbMySQLCell) * __shape[0] * __shape[1];
  }
  return tmp;
}

int32_t cbGraphSharedMem::getCellNum() {
  int32_t tmp = m_dataPool.size();
  for (auto& item : m_dataFromDevice) {
    auto __shape = item->getShape();
    tmp += __shape[0] * __shape[1];
  }
  return tmp;
}

cbNode::cbNode(const nodeType& nt) : nodeT(nt) {}

void cbNode::PointTo(cbNode* ptr) { nextNode = ptr; }

WFMySQLTask* cbNode::asSQLTask(void* metaTask) { return (WFMySQLTask*)metaTask; }

WFGoTask* cbNode::asGoTask(void* metaTask) { return (WFGoTask*)metaTask; }

cbVirtualDeviceNode::~cbVirtualDeviceNode() {
  // no members need to been free
}

void* cbVirtualDeviceNode::generateTask() {
  if (m_queries.size() != 1) {
    fmt::print(fg(fmt::color::red),
               "We have not support multi SQL sentence for now\n It's just for testing purpose\n");
    return nullptr;
  }
  return (void*)m_device->query(m_queries[0], [=](WFMySQLTask* task) -> void {
    // 1. Judge the graph is bind to this node.
    if (graph == nullptr) {
      fmt::print(fg(fmt::color::red), "Graph is not setted for this node.\n");
    }
    // 2. If the task is success.
    if (task->get_state() != WFT_STATE_SUCCESS) {
      fprintf(stderr, "task error=%d\n", task->get_error());
      return;
    }
    // 3. get the cursor.
    protocol::MySQLResultCursor cursor(task->get_resp());

    // 4. Judge the packet is ok.
    if (task->get_resp()->get_packet_type() == MYSQL_PACKET_ERROR) {
      fprintf(stderr, "ERROR. error_code=%d %s\n", task->get_resp()->get_error_code(),
              task->get_resp()->get_error_msg().c_str());
    }

    std::cout << cursor.fetch_fields()[0]->get_name() << std::endl;
    return;

    // store all data.
    cbVirtualSharedTable* __data = new cbVirtualSharedTable(&cursor);
    graph->m_sharedMem->push(__data);

    // Store the virtual table to the io.O port. And pass to the next Node's inputs port.
    mapShared2Virtual(__data, &io.O);
    nextNode->io.I.push_back(io.O);
    return;
  });
}

void cbVirtualDeviceNode::addQuery(const std::string& q) { m_queries.push_back(q); }

void cbVirtualDeviceNode::setMySQLDevice(trivial::cbMySqlDevice* device) { m_device = device; }

cbVirtualDeviceNode::cbVirtualDeviceNode() : cbNode(nodeType::Leaf) {}

cbOperatorNode::~cbOperatorNode() { delete Op; }

cbOperatorNode::cbOperatorNode(baseOp* op) : cbNode(nodeType::Operator), Op(op) {}

void* cbOperatorNode::generateTask() {
  // TODO
  return nullptr;
}

cbComputeGraph::cbComputeGraph(int32_t idx)
    : m_idx(idx),
      m_sharedMem(new cbGraphSharedMem()),
      m_sharedLuaStack(new cbGraphSharedLuaStack()),
      m_waitGroup(1) {}

cbComputeGraph::~cbComputeGraph() {
  delete m_sharedMem;
  delete m_sharedLuaStack;
  for (auto item : m_nodes) { delete item; }
}

bool cbComputeGraph::isDAG() {
  if (!isSingleOutput()) return false;
  // n^2 is ok, the graph is not such big.
  for (auto item : m_nodes) {
    cbNode* cur = item;
    cbNode* next = cur;
    while (next->nextNode != nullptr) {
      next = next->nextNode;
      if (next == cur) { return false; }
    }
  }
  return true;
}

bool cbComputeGraph::isSingleOutput() {
  int __singleOutCnt = 0;
  for (auto& item : m_nodes) {
    if (item->nextNode == nullptr) { __singleOutCnt++; }
  }
  return __singleOutCnt == 1 ? true : false;
}

cbMySQLCell* cbComputeGraph::createCell() {
  cbMySQLCell* ans = new cbMySQLCell();
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(int value) {
  cbMySQLCell* ans = new cbMySQLCell(value);
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(float value) {
  cbMySQLCell* ans = new cbMySQLCell(value);
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(double value) {
  cbMySQLCell* ans = new cbMySQLCell(value);
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(unsigned long long value) {
  cbMySQLCell* ans = new cbMySQLCell(value);
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(const std::string& value) {
  cbMySQLCell* ans = new cbMySQLCell(value);
  m_sharedMem->push(ans);
  return ans;
}

cbMySQLCell* cbComputeGraph::createCell(const std::string& value, const cbMySQLType& t) {
  cbMySQLCell* ans = new cbMySQLCell(value, t);
  m_sharedMem->push(ans);
  return ans;
}

cbVirtualDeviceNode* cbComputeGraph::createVirtualDeviceNode(int32_t idx) {
  cbVirtualDeviceNode* ans = new cbVirtualDeviceNode();
  ans->setMySQLDevice(m_virtualDevice->getMySqlDevice(idx));
  this->registerNode(ans);
  return ans;
}

void cbComputeGraph::setVirtualDeviceManager(trivial::cbVirtualDeviceManager* virtualDeviceM) {
  m_virtualDevice = virtualDeviceM;
}

void cbComputeGraph::registerNode(cbNode* node) {
  m_nodes.push_back(node);
  node->graph = this;
}

int32_t cbComputeGraph::getId() { return m_idx; }

WFGraphTask* cbComputeGraph::generateGraphTask(const graph_callback& func) {
  WFGraphTask* graph = WFTaskFactory::create_graph_task([=](WFGraphTask* task) {
    fmt::print(fg(fmt::color::steel_blue) | fmt::emphasis::italic,
               "Graph task {} complete. Wakeup main process\n", m_idx);
    m_waitGroup.done();
  });
  if (!(isDAG() && isSingleOutput())) {
    fmt::print(fg(fmt::color::red), "The graph {} is not DAG or has multi output\n", m_idx);
    return nullptr;
  }
  std::map<cbNode*, WFGraphNode*> __cb2WF;
  for (auto item : m_nodes) {
    switch (item->nodeT) {
      case nodeType::Leaf:
        __cb2WF[item] = &graph->create_graph_node((WFMySQLTask*)item->generateTask());
        break;
      case nodeType::Operator:
        __cb2WF[item] = &graph->create_graph_node((WFGoTask*)item->generateTask());
        break;
      case nodeType::Output: break;
    }
  }
  for (auto item : m_nodes) {
    if (item->nextNode != nullptr) { (*__cb2WF[item])-- > (*__cb2WF[item->nextNode]); }
  }
  return graph;
}

void cbComputeGraph::execMain(WFGraphTask* task, cbComputeGraph* graph) {
  if (task != nullptr && graph != nullptr) {
    task->start();
    graph->m_waitGroup.wait();
  } else {
    fmt::print(fg(fmt::color::red), "The pointer to task or graph is nullptr\n");
  }
}

};  // namespace graph