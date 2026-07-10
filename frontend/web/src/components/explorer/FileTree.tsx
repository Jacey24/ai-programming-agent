import { ChevronDown, ChevronRight, FileCode2, FileJson, FileText, Folder, FolderOpen } from "lucide-react";

import type { WorkspaceFileEntry } from "../../types/api";

export interface TreeNode {
  name: string;
  path: string;
  type: "file" | "directory";
  size?: number;
  children: TreeNode[];
}

interface FileTreeProps {
  items: WorkspaceFileEntry[];
  expandedDirectories: string[];
  activeFilePath: string;
  onToggleDirectory: (path: string) => void;
  onOpenFile: (path: string) => void;
}

export function FileTree({ items, expandedDirectories, activeFilePath, onToggleDirectory, onOpenFile }: FileTreeProps) {
  const nodes = buildTree(items);
  if (!nodes.length) {
    return <div className="p-4 text-xs text-slate-500">Workspace is empty.</div>;
  }

  return (
    <div className="py-2">
      {nodes.map((node) => (
        <FileTreeNode
          key={node.path}
          node={node}
          depth={0}
          expandedDirectories={expandedDirectories}
          activeFilePath={activeFilePath}
          onToggleDirectory={onToggleDirectory}
          onOpenFile={onOpenFile}
        />
      ))}
    </div>
  );
}

function FileTreeNode({
  node,
  depth,
  expandedDirectories,
  activeFilePath,
  onToggleDirectory,
  onOpenFile,
}: {
  node: TreeNode;
  depth: number;
  expandedDirectories: string[];
  activeFilePath: string;
  onToggleDirectory: (path: string) => void;
  onOpenFile: (path: string) => void;
}) {
  const expanded = expandedDirectories.includes(node.path);
  const isFile = node.type === "file";
  const selected = isFile && node.path === activeFilePath;
  const Icon = isFile ? iconForFile(node.name) : expanded ? FolderOpen : Folder;
  const Chevron = expanded ? ChevronDown : ChevronRight;

  return (
    <div>
      <button
        type="button"
        onClick={() => (isFile ? onOpenFile(node.path) : onToggleDirectory(node.path))}
        className={`flex h-7 w-full items-center gap-1.5 px-2 text-left text-xs ${
          selected ? "bg-cyan-400/12 text-cyan-100" : "text-slate-400 hover:bg-white/5 hover:text-slate-100"
        }`}
        style={{ paddingLeft: `${8 + depth * 14}px` }}
      >
        {isFile ? <span className="h-3.5 w-3.5" /> : <Chevron className="h-3.5 w-3.5 shrink-0 text-slate-500" />}
        <Icon className={`h-3.5 w-3.5 shrink-0 ${isFile ? "text-slate-400" : "text-cyan-300"}`} />
        <span className="min-w-0 flex-1 truncate">{node.name}</span>
      </button>
      {!isFile && expanded
        ? node.children.map((child) => (
            <FileTreeNode
              key={child.path}
              node={child}
              depth={depth + 1}
              expandedDirectories={expandedDirectories}
              activeFilePath={activeFilePath}
              onToggleDirectory={onToggleDirectory}
              onOpenFile={onOpenFile}
            />
          ))
        : null}
    </div>
  );
}

export function buildTree(items: WorkspaceFileEntry[]): TreeNode[] {
  const root: TreeNode[] = [];
  const byPath = new Map<string, TreeNode>();

  for (const item of items) {
    const parts = item.path.split("/").filter(Boolean);
    let siblings = root;
    let currentPath = "";

    parts.forEach((part, index) => {
      currentPath = currentPath ? `${currentPath}/${part}` : part;
      const existing = byPath.get(currentPath);
      if (existing) {
        siblings = existing.children;
        return;
      }

      const leaf = index === parts.length - 1;
      const node: TreeNode = {
        name: leaf ? item.name : part,
        path: currentPath,
        type: leaf ? item.type : "directory",
        size: leaf ? item.size : 0,
        children: [],
      };
      byPath.set(currentPath, node);
      siblings.push(node);
      siblings = node.children;
    });
  }

  return sortNodes(root);
}

function sortNodes(nodes: TreeNode[]): TreeNode[] {
  return nodes
    .map((node) => ({ ...node, children: sortNodes(node.children) }))
    .sort((a, b) => {
      if (a.type !== b.type) {
        return a.type === "directory" ? -1 : 1;
      }
      return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
    });
}

function iconForFile(name: string) {
  if (name.endsWith(".json")) return FileJson;
  if (name.endsWith(".md") || name.endsWith(".txt") || name.endsWith(".log")) return FileText;
  return FileCode2;
}
