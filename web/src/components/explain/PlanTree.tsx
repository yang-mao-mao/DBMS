import { Background, Controls, ReactFlow, type Edge, type Node } from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import type { ReactNode } from "react";
import type { PlanNode } from "../../types/database";
import { PlanNodeCard } from "./PlanNodeCard";

interface PlanTreeProps {
  plan: PlanNode;
}

interface FlowData extends Record<string, unknown> {
  label: ReactNode;
}

const layoutPlan = (root: PlanNode): { nodes: Node<FlowData>[]; edges: Edge[] } => {
  const nodes: Node<FlowData>[] = [];
  const edges: Edge[] = [];
  let leafIndex = 0;

  const visit = (node: PlanNode, depth: number): number => {
    const childPositions = node.children.map((child) => visit(child, depth + 1));
    const x = childPositions.length > 0
      ? childPositions.reduce((sum, position) => sum + position, 0) / childPositions.length
      : leafIndex++ * 250;
    nodes.push({ id: node.id, position: { x, y: depth * 145 }, data: { label: <PlanNodeCard node={node} /> }, style: { padding: 0, border: 0, background: "transparent", width: 176 } });
    for (const child of node.children) edges.push({ id: `${node.id}-${child.id}`, source: node.id, target: child.id, animated: false, style: { stroke: "#5B8CFF" } });
    return x;
  };
  visit(root, 0);
  return { nodes, edges };
};

export function PlanTree({ plan }: PlanTreeProps) {
  const { nodes, edges } = layoutPlan(plan);
  return (
    <div className="h-full min-h-60 rounded-lg border border-[var(--border)] bg-[var(--background)]">
      <ReactFlow nodes={nodes} edges={edges} fitView nodesDraggable={false} nodesConnectable={false} panOnScroll>
        <Background color="var(--border)" gap={22} size={1} />
        <Controls showInteractive={false} />
      </ReactFlow>
    </div>
  );
}
