import { createContext, useContext, useMemo } from 'react'
import { api } from '../services/api'
import { useResource } from '../hooks/useResource'
const Context = createContext(null)
export function CatalogProvider({ children }) {
  const resource = useResource(api.listProducts)
  const products = resource.data || []
  const categories = useMemo(
    () => [...new Set(products.map((p) => p.category).filter(Boolean))].sort(),
    [resource.data],
  )
  return (
    <Context.Provider value={{ ...resource, products, categories }}>{children}</Context.Provider>
  )
}
export const useCatalog = () => useContext(Context)
