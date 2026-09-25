import { useState } from 'react'
import { api } from '../../services/api'
import { usePagedResource } from '../../hooks/usePagedResource'
import LoadMore from '../../components/common/LoadMore'
import { Button, EmptyState, ErrorState, Loader, PageHeading } from '../../components/common/UI'
import { date } from '../../utils/format'
export default function AdminAuditPage() {
  const resource = usePagedResource(api.getAuditLogs)
  const { data, loading, error, reload } = resource
  const [query, setQuery] = useState('')
  const rows = (data || [])
    .filter((a) =>
      `${a.actorId} ${a.action} ${a.targetId}`.toLowerCase().includes(query.toLowerCase()),
    )
    .sort((a, b) => b.createdAt - a.createdAt)
  return (
    <>
      <PageHeading eyebrow="ACCOUNTABILITY" title="Admin audit log">
        Recorded staff changes to products, orders, shipping, uploads and roles.
      </PageHeading>
      <p className="muted">Search applies to loaded records. Load more to include older records.</p>
      <div className="panel">
        <div className="table-toolbar">
          <input
            aria-label="Search audit log"
            placeholder="Action, staff ID or record ID…"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          <Button variant="text" onClick={reload}>
            Refresh
          </Button>
        </div>
        {loading ? (
          <Loader />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !rows.length ? (
          <EmptyState title="No audit records found" />
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Staff ID</th>
                  <th>Action</th>
                  <th>Record</th>
                  <th>Details</th>
                </tr>
              </thead>
              <tbody>
                {rows.map((a) => (
                  <tr key={a.id}>
                    <td>{date(a.createdAt)}</td>
                    <td className="break-id">{a.actorId || 'System'}</td>
                    <td>{a.action.replaceAll('_', ' ')}</td>
                    <td className="break-id">{a.targetId}</td>
                    <td>
                      <details>
                        <summary>View changes</summary>
                        <pre
                          style={{
                            maxWidth: '24rem',
                            whiteSpace: 'pre-wrap',
                            overflowWrap: 'anywhere',
                          }}
                        >
                          {JSON.stringify(a.changes, null, 2)}
                        </pre>
                      </details>
                    </td>
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
