import { useState } from 'react'
import { api } from '../../services/api'
import { usePagedResource } from '../../hooks/usePagedResource'
import LoadMore from '../../components/common/LoadMore'
import { EmptyState, ErrorState, Loader, PageHeading } from '../../components/common/UI'
import { date } from '../../utils/format'
import { Link } from 'react-router-dom'
export default function AdminInventoryPage() {
  const resource = usePagedResource(api.getInventoryHistory)
  const { data, loading, error, reload } = resource
  const [query, setQuery] = useState('')
  const rows = (data || [])
    .filter((i) => `${i.productId} ${i.orderId || ''}`.toLowerCase().includes(query.toLowerCase()))
    .sort((a, b) => b.createdAt - a.createdAt)
  return (
    <>
      <PageHeading eyebrow="STOCK RECORDS" title="Inventory history">
        Stock changes recorded from product updates, orders and cancellations.
      </PageHeading>
      <p className="muted">
        History starts when Stage 2 is activated. Earlier stock changes are not reconstructed.
      </p>
      <p className="muted">Search applies to loaded records. Load more to include older records.</p>
      <div className="panel">
        <div className="table-toolbar">
          <input
            type="search"
            aria-label="Search inventory history"
            placeholder="Product or order ID…"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
        </div>
        {loading ? (
          <Loader />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !rows.length ? (
          <EmptyState title="No stock movements found" />
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Product</th>
                  <th>Change</th>
                  <th>Stock before / after</th>
                  <th>Reason</th>
                  <th>Order</th>
                  <th>Actor</th>
                </tr>
              </thead>
              <tbody>
                {rows.map((i) => (
                  <tr key={i.id}>
                    <td>{date(i.createdAt)}</td>
                    <td className="break-id">{i.productId}</td>
                    <td>
                      {i.delta > 0 ? '+' : ''}
                      {i.delta}
                    </td>
                    <td>
                      {i.stockBefore} → {i.stockAfter}
                    </td>
                    <td>
                      {i.reason.replaceAll('_', ' ')}
                      {i.note && <p className="field-help">{i.note}</p>}
                    </td>
                    <td>
                      {i.orderId ? <Link to={`/orders/${i.orderId}`}>{i.orderId}</Link> : '—'}
                    </td>
                    <td className="break-id">{i.actorId || '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
      <LoadMore resource={resource} />
    </>
  )
}
