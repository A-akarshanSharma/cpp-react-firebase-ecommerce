import { useCallback, useEffect, useRef, useState } from 'react'
export function useResource(loader) {
  const [state, setState] = useState({ data: null, loading: true, error: '' })
  const version = useRef(0)
  const reload = useCallback(async () => {
    const current = ++version.current
    setState((s) => ({ ...s, loading: true, error: '' }))
    try {
      const data = await loader()
      if (current === version.current) setState({ data, loading: false, error: '' })
      return data
    } catch (e) {
      if (current === version.current) setState({ data: null, loading: false, error: e.message })
      return null
    }
  }, [loader])
  useEffect(() => {
    reload()
    return () => {
      ++version.current
    }
  }, [reload])
  return { ...state, reload }
}
