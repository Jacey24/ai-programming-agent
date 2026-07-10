import { useCallback, useEffect, useReducer } from "react";

import { getWorkspaceFileContent, getWorkspaceTree } from "../api/workspaceApi";
import {
  initialWorkbenchState,
  workbenchReducer,
  type WorkbenchState,
} from "../store/workbenchReducer";

function messageFromError(error: unknown) {
  return error instanceof Error ? error.message : String(error);
}

export function useWorkspaceWorkbench({ workspaceId }: { workspaceId?: string }) {
  const [state, dispatch] = useReducer(workbenchReducer, initialWorkbenchState);

  const refreshTree = useCallback(async () => {
    if (!workspaceId) {
      return;
    }
    dispatch({ type: "treeLoading" });
    try {
      const tree = await getWorkspaceTree(workspaceId, "", 4);
      dispatch({ type: "treeLoaded", items: tree.items || [] });
    } catch (error) {
      dispatch({ type: "treeFailed", error: messageFromError(error) });
    }
  }, [workspaceId]);

  const openFile = useCallback(
    async (path: string) => {
      if (!workspaceId || !path) {
        return;
      }
      dispatch({ type: "fileOpenStart", path });
      try {
        const file = await getWorkspaceFileContent(workspaceId, path);
        dispatch({ type: "fileOpenSuccess", file });
      } catch (error) {
        dispatch({ type: "fileOpenFailed", path, error: messageFromError(error) });
      }
    },
    [workspaceId],
  );

  const closeFile = useCallback((path: string) => {
    dispatch({ type: "closeFile", path });
  }, []);

  const selectFile = useCallback((path: string) => {
    dispatch({ type: "selectFile", path });
  }, []);

  const toggleDirectory = useCallback((path: string) => {
    dispatch({ type: "toggleDirectory", path });
  }, []);

  const setBottomPanel = useCallback((panel: WorkbenchState["bottomPanel"]) => {
    dispatch({ type: "setBottomPanel", panel });
  }, []);

  useEffect(() => {
    void refreshTree();
  }, [refreshTree]);

  return {
    state,
    refreshTree,
    openFile,
    closeFile,
    selectFile,
    toggleDirectory,
    setBottomPanel,
  };
}
